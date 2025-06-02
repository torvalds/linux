/*
 * Public API and common code for kernel->userspace relay file support.
 *
 * See Documentation/filesystems/relay.rst for an overview.
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
#include <linux/export.h>
#include <linux/string.h>
#include <linux/relay.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/splice.h>

/* list of open channels, for cpu hotplug */
static DEFINE_MUTEX(relay_channels_mutex);
static LIST_HEAD(relay_channels);

/*
 * fault() vm_op implementation for relay file mapping.
 */
static vm_fault_t relay_buf_fault(struct vm_fault *vmf)
{
	struct page *page;
	struct rchan_buf *buf = vmf->vma->vm_private_data;
	pgoff_t pgoff = vmf->pgoff;

	if (!buf)
		return VM_FAULT_OOM;

	page = vmalloc_to_page(buf->start + (pgoff << PAGE_SHIFT));
	if (!page)
		return VM_FAULT_SIGBUS;
	get_page(page);
	vmf->page = page;

	return 0;
}

/*
 * vm_ops for relay file mappings.
 */
static const struct vm_operations_struct relay_file_mmap_ops = {
	.fault = relay_buf_fault,
};

/*
 * allocate an array of pointers of struct page
 */
static struct page **relay_alloc_page_array(unsigned int n_pages)
{
	return kvcalloc(n_pages, sizeof(struct page *), GFP_KERNEL);
}

/*
 * free an array of pointers of struct page
 */
static void relay_free_page_array(struct page **array)
{
	kvfree(array);
}

/**
 *	relay_mmap_buf: - mmap channel buffer to process address space
 *	@buf: relay channel buffer
 *	@vma: vm_area_struct describing memory to be mapped
 *
 *	Returns 0 if ok, negative on error
 *
 *	Caller should already have grabbed mmap_lock.
 */
static int relay_mmap_buf(struct rchan_buf *buf, struct vm_area_struct *vma)
{
	unsigned long length = vma->vm_end - vma->vm_start;

	if (!buf)
		return -EBADF;

	if (length != (unsigned long)buf->chan->alloc_size)
		return -EINVAL;

	vma->vm_ops = &relay_file_mmap_ops;
	vm_flags_set(vma, VM_DONTEXPAND);
	vma->vm_private_data = buf;

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

	buf->page_array = relay_alloc_page_array(n_pages);
	if (!buf->page_array)
		return NULL;

	for (i = 0; i < n_pages; i++) {
		buf->page_array[i] = alloc_page(GFP_KERNEL);
		if (unlikely(!buf->page_array[i]))
			goto depopulate;
		set_page_private(buf->page_array[i], (unsigned long)buf);
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
	relay_free_page_array(buf->page_array);
	return NULL;
}

/**
 *	relay_create_buf - allocate and initialize a channel buffer
 *	@chan: the relay channel
 *
 *	Returns channel buffer if successful, %NULL otherwise.
 */
static struct rchan_buf *relay_create_buf(struct rchan *chan)
{
	struct rchan_buf *buf;

	if (chan->n_subbufs > KMALLOC_MAX_SIZE / sizeof(size_t))
		return NULL;

	buf = kzalloc(sizeof(struct rchan_buf), GFP_KERNEL);
	if (!buf)
		return NULL;
	buf->padding = kmalloc_array(chan->n_subbufs, sizeof(size_t),
				     GFP_KERNEL);
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
static void relay_destroy_channel(struct kref *kref)
{
	struct rchan *chan = container_of(kref, struct rchan, kref);
	free_percpu(chan->buf);
	kfree(chan);
}

/**
 *	relay_destroy_buf - destroy an rchan_buf struct and associated buffer
 *	@buf: the buffer struct
 */
static void relay_destroy_buf(struct rchan_buf *buf)
{
	struct rchan *chan = buf->chan;
	unsigned int i;

	if (likely(buf->start)) {
		vunmap(buf->start);
		for (i = 0; i < buf->page_count; i++)
			__free_page(buf->page_array[i]);
		relay_free_page_array(buf->page_array);
	}
	*per_cpu_ptr(chan->buf, buf->cpu) = NULL;
	kfree(buf->padding);
	kfree(buf);
	kref_put(&chan->kref, relay_destroy_channel);
}

/**
 *	relay_remove_buf - remove a channel buffer
 *	@kref: target kernel reference that contains the relay buffer
 *
 *	Removes the file from the filesystem, which also frees the
 *	rchan_buf_struct and the channel buffer.  Should only be called from
 *	kref_put().
 */
static void relay_remove_buf(struct kref *kref)
{
	struct rchan_buf *buf = container_of(kref, struct rchan_buf, kref);
	relay_destroy_buf(buf);
}

/**
 *	relay_buf_empty - boolean, is the channel buffer empty?
 *	@buf: channel buffer
 *
 *	Returns 1 if the buffer is empty, 0 otherwise.
 */
static int relay_buf_empty(struct rchan_buf *buf)
{
	return (buf->subbufs_produced - buf->subbufs_consumed) ? 0 : 1;
}

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

static int relay_subbuf_start(struct rchan_buf *buf, void *subbuf,
			      void *prev_subbuf, size_t prev_padding)
{
	if (!buf->chan->cb->subbuf_start)
		return !relay_buf_full(buf);

	return buf->chan->cb->subbuf_start(buf, subbuf,
					   prev_subbuf, prev_padding);
}

/**
 *	wakeup_readers - wake up readers waiting on a channel
 *	@work: contains the channel buffer
 *
 *	This is the function used to defer reader waking
 */
static void wakeup_readers(struct irq_work *work)
{
	struct rchan_buf *buf;

	buf = container_of(work, struct rchan_buf, wakeup_work);
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
		init_irq_work(&buf->wakeup_work, wakeup_readers);
	} else {
		irq_work_sync(&buf->wakeup_work);
	}

	buf->subbufs_produced = 0;
	buf->subbufs_consumed = 0;
	buf->bytes_consumed = 0;
	buf->finalized = 0;
	buf->data = buf->start;
	buf->offset = 0;

	for (i = 0; i < buf->chan->n_subbufs; i++)
		buf->padding[i] = 0;

	relay_subbuf_start(buf, buf->data, NULL, 0);
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
	struct rchan_buf *buf;
	unsigned int i;

	if (!chan)
		return;

	if (chan->is_global && (buf = *per_cpu_ptr(chan->buf, 0))) {
		__relay_reset(buf, 0);
		return;
	}

	mutex_lock(&relay_channels_mutex);
	for_each_possible_cpu(i)
		if ((buf = *per_cpu_ptr(chan->buf, i)))
			__relay_reset(buf, 0);
	mutex_unlock(&relay_channels_mutex);
}
EXPORT_SYMBOL_GPL(relay_reset);

static inline void relay_set_buf_dentry(struct rchan_buf *buf,
					struct dentry *dentry)
{
	buf->dentry = dentry;
	d_inode(buf->dentry)->i_size = buf->early_bytes;
}

static struct dentry *relay_create_buf_file(struct rchan *chan,
					    struct rchan_buf *buf,
					    unsigned int cpu)
{
	struct dentry *dentry;
	char *tmpname;

	tmpname = kasprintf(GFP_KERNEL, "%s%d", chan->base_filename, cpu);
	if (!tmpname)
		return NULL;

	/* Create file in fs */
	dentry = chan->cb->create_buf_file(tmpname, chan->parent,
					   S_IRUSR, buf,
					   &chan->is_global);
	if (IS_ERR(dentry))
		dentry = NULL;

	kfree(tmpname);

	return dentry;
}

/*
 *	relay_open_buf - create a new relay channel buffer
 *
 *	used by relay_open() and CPU hotplug.
 */
static struct rchan_buf *relay_open_buf(struct rchan *chan, unsigned int cpu)
{
	struct rchan_buf *buf;
	struct dentry *dentry;

 	if (chan->is_global)
		return *per_cpu_ptr(chan->buf, 0);

	buf = relay_create_buf(chan);
	if (!buf)
		return NULL;

	if (chan->has_base_filename) {
		dentry = relay_create_buf_file(chan, buf, cpu);
		if (!dentry)
			goto free_buf;
		relay_set_buf_dentry(buf, dentry);
	} else {
		/* Only retrieve global info, nothing more, nothing less */
		dentry = chan->cb->create_buf_file(NULL, NULL,
						   S_IRUSR, buf,
						   &chan->is_global);
		if (IS_ERR_OR_NULL(dentry))
			goto free_buf;
	}

 	buf->cpu = cpu;
 	__relay_reset(buf, 1);

 	if(chan->is_global) {
		*per_cpu_ptr(chan->buf, 0) = buf;
 		buf->cpu = 0;
  	}

	return buf;

free_buf:
 	relay_destroy_buf(buf);
	return NULL;
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
	irq_work_sync(&buf->wakeup_work);
	buf->chan->cb->remove_buf_file(buf->dentry);
	kref_put(&buf->kref, relay_remove_buf);
}

int relay_prepare_cpu(unsigned int cpu)
{
	struct rchan *chan;
	struct rchan_buf *buf;

	mutex_lock(&relay_channels_mutex);
	list_for_each_entry(chan, &relay_channels, list) {
		if (*per_cpu_ptr(chan->buf, cpu))
			continue;
		buf = relay_open_buf(chan, cpu);
		if (!buf) {
			pr_err("relay: cpu %d buffer creation failed\n", cpu);
			mutex_unlock(&relay_channels_mutex);
			return -ENOMEM;
		}
		*per_cpu_ptr(chan->buf, cpu) = buf;
	}
	mutex_unlock(&relay_channels_mutex);
	return 0;
}

/**
 *	relay_open - create a new relay channel
 *	@base_filename: base name of files to create, %NULL for buffering only
 *	@parent: dentry of parent directory, %NULL for root directory or buffer
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
 *
 *	If opening a buffer (@parent = NULL) that you later wish to register
 *	in a filesystem, call relay_late_setup_files() once the @parent dentry
 *	is available.
 */
struct rchan *relay_open(const char *base_filename,
			 struct dentry *parent,
			 size_t subbuf_size,
			 size_t n_subbufs,
			 const struct rchan_callbacks *cb,
			 void *private_data)
{
	unsigned int i;
	struct rchan *chan;
	struct rchan_buf *buf;

	if (!(subbuf_size && n_subbufs))
		return NULL;
	if (subbuf_size > UINT_MAX / n_subbufs)
		return NULL;
	if (!cb || !cb->create_buf_file || !cb->remove_buf_file)
		return NULL;

	chan = kzalloc(sizeof(struct rchan), GFP_KERNEL);
	if (!chan)
		return NULL;

	chan->buf = alloc_percpu(struct rchan_buf *);
	if (!chan->buf) {
		kfree(chan);
		return NULL;
	}

	chan->version = RELAYFS_CHANNEL_VERSION;
	chan->n_subbufs = n_subbufs;
	chan->subbuf_size = subbuf_size;
	chan->alloc_size = PAGE_ALIGN(subbuf_size * n_subbufs);
	chan->parent = parent;
	chan->private_data = private_data;
	if (base_filename) {
		chan->has_base_filename = 1;
		strscpy(chan->base_filename, base_filename, NAME_MAX);
	}
	chan->cb = cb;
	kref_init(&chan->kref);

	mutex_lock(&relay_channels_mutex);
	for_each_online_cpu(i) {
		buf = relay_open_buf(chan, i);
		if (!buf)
			goto free_bufs;
		*per_cpu_ptr(chan->buf, i) = buf;
	}
	list_add(&chan->list, &relay_channels);
	mutex_unlock(&relay_channels_mutex);

	return chan;

free_bufs:
	for_each_possible_cpu(i) {
		if ((buf = *per_cpu_ptr(chan->buf, i)))
			relay_close_buf(buf);
	}

	kref_put(&chan->kref, relay_destroy_channel);
	mutex_unlock(&relay_channels_mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(relay_open);

struct rchan_percpu_buf_dispatcher {
	struct rchan_buf *buf;
	struct dentry *dentry;
};

/* Called in atomic context. */
static void __relay_set_buf_dentry(void *info)
{
	struct rchan_percpu_buf_dispatcher *p = info;

	relay_set_buf_dentry(p->buf, p->dentry);
}

/**
 *	relay_late_setup_files - triggers file creation
 *	@chan: channel to operate on
 *	@base_filename: base name of files to create
 *	@parent: dentry of parent directory, %NULL for root directory
 *
 *	Returns 0 if successful, non-zero otherwise.
 *
 *	Use to setup files for a previously buffer-only channel created
 *	by relay_open() with a NULL parent dentry.
 *
 *	For example, this is useful for perfomring early tracing in kernel,
 *	before VFS is up and then exposing the early results once the dentry
 *	is available.
 */
int relay_late_setup_files(struct rchan *chan,
			   const char *base_filename,
			   struct dentry *parent)
{
	int err = 0;
	unsigned int i, curr_cpu;
	unsigned long flags;
	struct dentry *dentry;
	struct rchan_buf *buf;
	struct rchan_percpu_buf_dispatcher disp;

	if (!chan || !base_filename)
		return -EINVAL;

	strscpy(chan->base_filename, base_filename, NAME_MAX);

	mutex_lock(&relay_channels_mutex);
	/* Is chan already set up? */
	if (unlikely(chan->has_base_filename)) {
		mutex_unlock(&relay_channels_mutex);
		return -EEXIST;
	}
	chan->has_base_filename = 1;
	chan->parent = parent;

	if (chan->is_global) {
		err = -EINVAL;
		buf = *per_cpu_ptr(chan->buf, 0);
		if (!WARN_ON_ONCE(!buf)) {
			dentry = relay_create_buf_file(chan, buf, 0);
			if (dentry && !WARN_ON_ONCE(!chan->is_global)) {
				relay_set_buf_dentry(buf, dentry);
				err = 0;
			}
		}
		mutex_unlock(&relay_channels_mutex);
		return err;
	}

	curr_cpu = get_cpu();
	/*
	 * The CPU hotplug notifier ran before us and created buffers with
	 * no files associated. So it's safe to call relay_setup_buf_file()
	 * on all currently online CPUs.
	 */
	for_each_online_cpu(i) {
		buf = *per_cpu_ptr(chan->buf, i);
		if (unlikely(!buf)) {
			WARN_ONCE(1, KERN_ERR "CPU has no buffer!\n");
			err = -EINVAL;
			break;
		}

		dentry = relay_create_buf_file(chan, buf, i);
		if (unlikely(!dentry)) {
			err = -EINVAL;
			break;
		}

		if (curr_cpu == i) {
			local_irq_save(flags);
			relay_set_buf_dentry(buf, dentry);
			local_irq_restore(flags);
		} else {
			disp.buf = buf;
			disp.dentry = dentry;
			smp_mb();
			/* relay_channels_mutex must be held, so wait. */
			err = smp_call_function_single(i,
						       __relay_set_buf_dentry,
						       &disp, 1);
		}
		if (unlikely(err))
			break;
	}
	put_cpu();
	mutex_unlock(&relay_channels_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(relay_late_setup_files);

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
		if (buf->dentry)
			d_inode(buf->dentry)->i_size +=
				buf->chan->subbuf_size -
				buf->padding[old_subbuf];
		else
			buf->early_bytes += buf->chan->subbuf_size -
					    buf->padding[old_subbuf];
		smp_mb();
		if (waitqueue_active(&buf->read_wait)) {
			/*
			 * Calling wake_up_interruptible() from here
			 * will deadlock if we happen to be logging
			 * from the scheduler (trying to re-grab
			 * rq->lock), so defer it.
			 */
			irq_work_queue(&buf->wakeup_work);
		}
	}

	old = buf->data;
	new_subbuf = buf->subbufs_produced % buf->chan->n_subbufs;
	new = buf->start + new_subbuf * buf->chan->subbuf_size;
	buf->offset = 0;
	if (!relay_subbuf_start(buf, new, old, buf->prev_padding)) {
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

	if (!chan || cpu >= NR_CPUS)
		return;

	buf = *per_cpu_ptr(chan->buf, cpu);
	if (!buf || subbufs_consumed > chan->n_subbufs)
		return;

	if (subbufs_consumed > buf->subbufs_produced - buf->subbufs_consumed)
		buf->subbufs_consumed = buf->subbufs_produced;
	else
		buf->subbufs_consumed += subbufs_consumed;
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
	struct rchan_buf *buf;
	unsigned int i;

	if (!chan)
		return;

	mutex_lock(&relay_channels_mutex);
	if (chan->is_global && (buf = *per_cpu_ptr(chan->buf, 0)))
		relay_close_buf(buf);
	else
		for_each_possible_cpu(i)
			if ((buf = *per_cpu_ptr(chan->buf, i)))
				relay_close_buf(buf);

	if (chan->last_toobig)
		printk(KERN_WARNING "relay: one or more items not logged "
		       "[item size (%zd) > sub-buffer size (%zd)]\n",
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
	struct rchan_buf *buf;
	unsigned int i;

	if (!chan)
		return;

	if (chan->is_global && (buf = *per_cpu_ptr(chan->buf, 0))) {
		relay_switch_subbuf(buf, 0);
		return;
	}

	mutex_lock(&relay_channels_mutex);
	for_each_possible_cpu(i)
		if ((buf = *per_cpu_ptr(chan->buf, i)))
			relay_switch_subbuf(buf, 0);
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

	return nonseekable_open(inode, filp);
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
static __poll_t relay_file_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = 0;
	struct rchan_buf *buf = filp->private_data;

	if (buf->finalized)
		return EPOLLERR;

	if (filp->f_mode & FMODE_READ) {
		poll_wait(filp, &buf->read_wait, wait);
		if (!relay_buf_empty(buf))
			mask |= EPOLLIN | EPOLLRDNORM;
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

	if (buf->subbufs_produced == buf->subbufs_consumed &&
	    buf->offset == buf->bytes_consumed)
		return;

	if (buf->bytes_consumed + bytes_consumed > subbuf_size) {
		relay_subbufs_consumed(buf->chan, buf->cpu, 1);
		buf->bytes_consumed = 0;
	}

	buf->bytes_consumed += bytes_consumed;
	if (!read_pos)
		read_subbuf = buf->subbufs_consumed % n_subbufs;
	else
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
static int relay_file_read_avail(struct rchan_buf *buf)
{
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;
	size_t produced = buf->subbufs_produced;
	size_t consumed;

	relay_file_read_consume(buf, 0, 0);

	consumed = buf->subbufs_consumed;

	if (unlikely(buf->offset > subbuf_size)) {
		if (produced == consumed)
			return 0;
		return 1;
	}

	if (unlikely(produced - consumed >= n_subbufs)) {
		consumed = produced - n_subbufs + 1;
		buf->subbufs_consumed = consumed;
		buf->bytes_consumed = 0;
	}

	produced = (produced % n_subbufs) * subbuf_size + buf->offset;
	consumed = (consumed % n_subbufs) * subbuf_size + buf->bytes_consumed;

	if (consumed > produced)
		produced += n_subbufs * subbuf_size;

	if (consumed == produced) {
		if (buf->offset == subbuf_size &&
		    buf->subbufs_produced > buf->subbufs_consumed)
			return 1;
		return 0;
	}

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
 *	@buf: relay channel buffer
 *
 *	If the read_pos is in the middle of padding, return the
 *	position of the first actually available byte, otherwise
 *	return the original value.
 */
static size_t relay_file_read_start_pos(struct rchan_buf *buf)
{
	size_t read_subbuf, padding, padding_start, padding_end;
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;
	size_t consumed = buf->subbufs_consumed % n_subbufs;
	size_t read_pos = (consumed * subbuf_size + buf->bytes_consumed)
			% (n_subbufs * subbuf_size);

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

static ssize_t relay_file_read(struct file *filp,
			       char __user *buffer,
			       size_t count,
			       loff_t *ppos)
{
	struct rchan_buf *buf = filp->private_data;
	size_t read_start, avail;
	size_t written = 0;
	int ret;

	if (!count)
		return 0;

	inode_lock(file_inode(filp));
	do {
		void *from;

		if (!relay_file_read_avail(buf))
			break;

		read_start = relay_file_read_start_pos(buf);
		avail = relay_file_read_subbuf_avail(read_start, buf);
		if (!avail)
			break;

		avail = min(count, avail);
		from = buf->start + read_start;
		ret = avail;
		if (copy_to_user(buffer, from, avail))
			break;

		buffer += ret;
		written += ret;
		count -= ret;

		relay_file_read_consume(buf, read_start, ret);
		*ppos = relay_file_read_end_pos(buf, read_start, ret);
	} while (count);
	inode_unlock(file_inode(filp));

	return written;
}


const struct file_operations relay_file_operations = {
	.open		= relay_file_open,
	.poll		= relay_file_poll,
	.mmap		= relay_file_mmap,
	.read		= relay_file_read,
	.release	= relay_file_release,
};
EXPORT_SYMBOL_GPL(relay_file_operations);
