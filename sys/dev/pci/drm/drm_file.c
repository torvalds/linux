/*
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/anon_inodes.h>
#include <linux/dma-fence.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_client.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/* from BKL pushdown */
DEFINE_MUTEX(drm_global_mutex);

bool drm_dev_needs_global_mutex(struct drm_device *dev)
{
	/*
	 * The deprecated ->load callback must be called after the driver is
	 * already registered. This means such drivers rely on the BKL to make
	 * sure an open can't proceed until the driver is actually fully set up.
	 * Similar hilarity holds for the unload callback.
	 */
	if (dev->driver->load || dev->driver->unload)
		return true;

	return false;
}

/**
 * DOC: file operations
 *
 * Drivers must define the file operations structure that forms the DRM
 * userspace API entry point, even though most of those operations are
 * implemented in the DRM core. The resulting &struct file_operations must be
 * stored in the &drm_driver.fops field. The mandatory functions are drm_open(),
 * drm_read(), drm_ioctl() and drm_compat_ioctl() if CONFIG_COMPAT is enabled
 * Note that drm_compat_ioctl will be NULL if CONFIG_COMPAT=n, so there's no
 * need to sprinkle #ifdef into the code. Drivers which implement private ioctls
 * that require 32/64 bit compatibility support must provide their own
 * &file_operations.compat_ioctl handler that processes private ioctls and calls
 * drm_compat_ioctl() for core ioctls.
 *
 * In addition drm_read() and drm_poll() provide support for DRM events. DRM
 * events are a generic and extensible means to send asynchronous events to
 * userspace through the file descriptor. They are used to send vblank event and
 * page flip completions by the KMS API. But drivers can also use it for their
 * own needs, e.g. to signal completion of rendering.
 *
 * For the driver-side event interface see drm_event_reserve_init() and
 * drm_send_event() as the main starting points.
 *
 * The memory mapping implementation will vary depending on how the driver
 * manages memory. For GEM-based drivers this is drm_gem_mmap().
 *
 * No other file operations are supported by the DRM userspace API. Overall the
 * following is an example &file_operations structure::
 *
 *     static const example_drm_fops = {
 *             .owner = THIS_MODULE,
 *             .open = drm_open,
 *             .release = drm_release,
 *             .unlocked_ioctl = drm_ioctl,
 *             .compat_ioctl = drm_compat_ioctl, // NULL if CONFIG_COMPAT=n
 *             .poll = drm_poll,
 *             .read = drm_read,
 *             .mmap = drm_gem_mmap,
 *     };
 *
 * For plain GEM based drivers there is the DEFINE_DRM_GEM_FOPS() macro, and for
 * DMA based drivers there is the DEFINE_DRM_GEM_DMA_FOPS() macro to make this
 * simpler.
 *
 * The driver's &file_operations must be stored in &drm_driver.fops.
 *
 * For driver-private IOCTL handling see the more detailed discussion in
 * :ref:`IOCTL support in the userland interfaces chapter<drm_driver_ioctl>`.
 */

/**
 * drm_file_alloc - allocate file context
 * @minor: minor to allocate on
 *
 * This allocates a new DRM file context. It is not linked into any context and
 * can be used by the caller freely. Note that the context keeps a pointer to
 * @minor, so it must be freed before @minor is.
 *
 * RETURNS:
 * Pointer to newly allocated context, ERR_PTR on failure.
 */
struct drm_file *drm_file_alloc(struct drm_minor *minor)
{
	static atomic64_t ident = ATOMIC64_INIT(0);
	struct drm_device *dev = minor->dev;
	struct drm_file *file;
	int ret;

	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		return ERR_PTR(-ENOMEM);

	/* Get a unique identifier for fdinfo: */
	file->client_id = atomic64_inc_return(&ident);
#ifdef __linux__
	rcu_assign_pointer(file->pid, get_pid(task_tgid(current)));
#endif
	file->minor = minor;

	/* for compatibility root is always authenticated */
	file->authenticated = capable(CAP_SYS_ADMIN);

	INIT_LIST_HEAD(&file->lhead);
	INIT_LIST_HEAD(&file->fbs);
	rw_init(&file->fbs_lock, "fbslk");
	INIT_LIST_HEAD(&file->blobs);
	INIT_LIST_HEAD(&file->pending_event_list);
	INIT_LIST_HEAD(&file->event_list);
	init_waitqueue_head(&file->event_wait);
	file->event_space = 4096; /* set aside 4k for event buffer */

	mtx_init(&file->master_lookup_lock, IPL_NONE);
	rw_init(&file->event_read_lock, "evread");

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_open(dev, file);

	if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		drm_syncobj_open(file);

	drm_prime_init_file_private(&file->prime);

	if (dev->driver->open) {
		ret = dev->driver->open(dev, file);
		if (ret < 0)
			goto out_prime_destroy;
	}

	return file;

out_prime_destroy:
	drm_prime_destroy_file_private(&file->prime);
	if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		drm_syncobj_release(file);
	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_release(dev, file);
	put_pid(rcu_access_pointer(file->pid));
	kfree(file);

	return ERR_PTR(ret);
}

static void drm_events_release(struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_pending_event *e, *et;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	/* Unlink pending events */
	list_for_each_entry_safe(e, et, &file_priv->pending_event_list,
				 pending_link) {
		list_del(&e->pending_link);
		e->file_priv = NULL;
	}

	/* Remove unconsumed events */
	list_for_each_entry_safe(e, et, &file_priv->event_list, link) {
		list_del(&e->link);
		kfree(e);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

/**
 * drm_file_free - free file context
 * @file: context to free, or NULL
 *
 * This destroys and deallocates a DRM file context previously allocated via
 * drm_file_alloc(). The caller must make sure to unlink it from any contexts
 * before calling this.
 *
 * If NULL is passed, this is a no-op.
 */
void drm_file_free(struct drm_file *file)
{
	struct drm_device *dev;

	if (!file)
		return;

	dev = file->minor->dev;

#ifdef __linux__
	drm_dbg_core(dev, "comm=\"%s\", pid=%d, dev=0x%lx, open_count=%d\n",
		     current->comm, task_pid_nr(current),
		     (long)old_encode_dev(file->minor->kdev->devt),
		     atomic_read(&dev->open_count));
#else
	drm_dbg_core(dev, "pid=%d, dev=0x%lx, open_count=%d\n",
		     curproc->p_p->ps_pid, (long)&dev->dev,
		     atomic_read(&dev->open_count));
#endif

	drm_events_release(file);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		drm_fb_release(file);
		drm_property_destroy_user_blobs(dev, file);
	}

	if (drm_core_check_feature(dev, DRIVER_SYNCOBJ))
		drm_syncobj_release(file);

	if (drm_core_check_feature(dev, DRIVER_GEM))
		drm_gem_release(dev, file);

	if (drm_is_primary_client(file))
		drm_master_release(file);

	if (dev->driver->postclose)
		dev->driver->postclose(dev, file);

	drm_prime_destroy_file_private(&file->prime);

	WARN_ON(!list_empty(&file->event_list));

	put_pid(rcu_access_pointer(file->pid));
	kfree(file);
}

#ifdef __linux__

static void drm_close_helper(struct file *filp)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;

	mutex_lock(&dev->filelist_mutex);
	list_del(&file_priv->lhead);
	mutex_unlock(&dev->filelist_mutex);

	drm_file_free(file_priv);
}

/*
 * Check whether DRI will run on this CPU.
 *
 * \return non-zero if the DRI will run on this CPU, or zero otherwise.
 */
static int drm_cpu_valid(void)
{
#if defined(__sparc__) && !defined(__sparc_v9__)
	return 0;		/* No cmpxchg before v9 sparc. */
#endif
	return 1;
}

/*
 * Called whenever a process opens a drm node
 *
 * \param filp file pointer.
 * \param minor acquired minor-object.
 * \return zero on success or a negative number on failure.
 *
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
int drm_open_helper(struct file *filp, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct drm_file *priv;
	int ret;

	if (filp->f_flags & O_EXCL)
		return -EBUSY;	/* No exclusive opens */
	if (!drm_cpu_valid())
		return -EINVAL;
	if (dev->switch_power_state != DRM_SWITCH_POWER_ON &&
	    dev->switch_power_state != DRM_SWITCH_POWER_DYNAMIC_OFF)
		return -EINVAL;
	if (WARN_ON_ONCE(!(filp->f_op->fop_flags & FOP_UNSIGNED_OFFSET)))
		return -EINVAL;

	drm_dbg_core(dev, "comm=\"%s\", pid=%d, minor=%d\n",
		     current->comm, task_pid_nr(current), minor->index);

	priv = drm_file_alloc(minor);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	if (drm_is_primary_client(priv)) {
		ret = drm_master_open(priv);
		if (ret) {
			drm_file_free(priv);
			return ret;
		}
	}

	filp->private_data = priv;
	priv->filp = filp;

	mutex_lock(&dev->filelist_mutex);
	list_add(&priv->lhead, &dev->filelist);
	mutex_unlock(&dev->filelist_mutex);

	return 0;
}

#endif /* __linux__ */

/**
 * drm_open - open method for DRM file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function must be used by drivers as their &file_operations.open method.
 * It looks up the correct DRM device and instantiates all the per-file
 * resources for it. It also calls the &drm_driver.open driver callback.
 *
 * RETURNS:
 * 0 on success or negative errno value on failure.
 */
#ifdef __linux__
int drm_open(struct inode *inode, struct file *filp)
{
	struct drm_device *dev;
	struct drm_minor *minor;
	int retcode;

	minor = drm_minor_acquire(&drm_minors_xa, iminor(inode));
	if (IS_ERR(minor))
		return PTR_ERR(minor);

	dev = minor->dev;
	if (drm_dev_needs_global_mutex(dev))
		mutex_lock(&drm_global_mutex);

	atomic_fetch_inc(&dev->open_count);

	/* share address_space across all char-devs of a single device */
	filp->f_mapping = dev->anon_inode->i_mapping;

	retcode = drm_open_helper(filp, minor);
	if (retcode)
		goto err_undo;

	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);

	return 0;

err_undo:
	atomic_dec(&dev->open_count);
	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);
	drm_minor_release(minor);
	return retcode;
}
EXPORT_SYMBOL(drm_open);
#endif

void drm_lastclose(struct drm_device *dev)
{
	drm_client_dev_restore(dev);

	if (dev_is_pci(dev->dev))
		vga_switcheroo_process_delayed_switch();
}

/**
 * drm_release - release method for DRM file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function must be used by drivers as their &file_operations.release
 * method. It frees any resources associated with the open file. If this
 * is the last open file for the DRM device, it also restores the active
 * in-kernel DRM client.
 *
 * RETURNS:
 * Always succeeds and returns 0.
 */
int drm_release(struct inode *inode, struct file *filp)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	struct drm_file *file_priv = filp->private_data;
	struct drm_minor *minor = file_priv->minor;
	struct drm_device *dev = minor->dev;

	if (drm_dev_needs_global_mutex(dev))
		mutex_lock(&drm_global_mutex);

	drm_dbg_core(dev, "open_count = %d\n", atomic_read(&dev->open_count));

	drm_close_helper(filp);

	if (atomic_dec_and_test(&dev->open_count))
		drm_lastclose(dev);

	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);

	drm_minor_release(minor);

	return 0;
#endif
}
EXPORT_SYMBOL(drm_release);

void drm_file_update_pid(struct drm_file *filp)
{
#ifdef notyet
	struct drm_device *dev;
	struct pid *pid, *old;
#endif

	/*
	 * Master nodes need to keep the original ownership in order for
	 * drm_master_check_perm to keep working correctly. (See comment in
	 * drm_auth.c.)
	 */
	if (filp->was_master)
		return;

	STUB();
#ifdef notyet
	pid = task_tgid(current);

	/*
	 * Quick unlocked check since the model is a single handover followed by
	 * exclusive repeated use.
	 */
	if (pid == rcu_access_pointer(filp->pid))
		return;

	dev = filp->minor->dev;
	mutex_lock(&dev->filelist_mutex);
	get_pid(pid);
	old = rcu_replace_pointer(filp->pid, pid, 1);
	mutex_unlock(&dev->filelist_mutex);

	synchronize_rcu();
	put_pid(old);
#endif
}

/**
 * drm_release_noglobal - release method for DRM file
 * @inode: device inode
 * @filp: file pointer.
 *
 * This function may be used by drivers as their &file_operations.release
 * method. It frees any resources associated with the open file prior to taking
 * the drm_global_mutex. If this is the last open file for the DRM device, it
 * then restores the active in-kernel DRM client.
 *
 * RETURNS:
 * Always succeeds and returns 0.
 */
int drm_release_noglobal(struct inode *inode, struct file *filp)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	struct drm_file *file_priv = filp->private_data;
	struct drm_minor *minor = file_priv->minor;
	struct drm_device *dev = minor->dev;

	drm_close_helper(filp);

	if (atomic_dec_and_mutex_lock(&dev->open_count, &drm_global_mutex)) {
		drm_lastclose(dev);
		mutex_unlock(&drm_global_mutex);
	}

	drm_minor_release(minor);

	return 0;
#endif
}
EXPORT_SYMBOL(drm_release_noglobal);

/**
 * drm_read - read method for DRM file
 * @filp: file pointer
 * @buffer: userspace destination pointer for the read
 * @count: count in bytes to read
 * @offset: offset to read
 *
 * This function must be used by drivers as their &file_operations.read
 * method if they use DRM events for asynchronous signalling to userspace.
 * Since events are used by the KMS API for vblank and page flip completion this
 * means all modern display drivers must use it.
 *
 * @offset is ignored, DRM events are read like a pipe. Polling support is
 * provided by drm_poll().
 *
 * This function will only ever read a full event. Therefore userspace must
 * supply a big enough buffer to fit any event to ensure forward progress. Since
 * the maximum event space is currently 4K it's recommended to just use that for
 * safety.
 *
 * RETURNS:
 * Number of bytes read (always aligned to full events, and can be 0) or a
 * negative error code on failure.
 */
ssize_t drm_read(struct file *filp, char __user *buffer,
		 size_t count, loff_t *offset)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	ssize_t ret;

	ret = mutex_lock_interruptible(&file_priv->event_read_lock);
	if (ret)
		return ret;

	for (;;) {
		struct drm_pending_event *e = NULL;

		spin_lock_irq(&dev->event_lock);
		if (!list_empty(&file_priv->event_list)) {
			e = list_first_entry(&file_priv->event_list,
					struct drm_pending_event, link);
			file_priv->event_space += e->event->length;
			list_del(&e->link);
		}
		spin_unlock_irq(&dev->event_lock);

		if (e == NULL) {
			if (ret)
				break;

			if (filp->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}

			mutex_unlock(&file_priv->event_read_lock);
			ret = wait_event_interruptible(file_priv->event_wait,
						       !list_empty(&file_priv->event_list));
			if (ret >= 0)
				ret = mutex_lock_interruptible(&file_priv->event_read_lock);
			if (ret)
				return ret;
		} else {
			unsigned length = e->event->length;

			if (length > count - ret) {
put_back_event:
				spin_lock_irq(&dev->event_lock);
				file_priv->event_space -= length;
				list_add(&e->link, &file_priv->event_list);
				spin_unlock_irq(&dev->event_lock);
				wake_up_interruptible_poll(&file_priv->event_wait,
					EPOLLIN | EPOLLRDNORM);
				break;
			}

			if (copy_to_user(buffer + ret, e->event, length)) {
				if (ret == 0)
					ret = -EFAULT;
				goto put_back_event;
			}

			ret += length;
			kfree(e);
		}
	}
	mutex_unlock(&file_priv->event_read_lock);

	return ret;
#endif
}
EXPORT_SYMBOL(drm_read);

#ifdef notyet
/**
 * drm_poll - poll method for DRM file
 * @filp: file pointer
 * @wait: poll waiter table
 *
 * This function must be used by drivers as their &file_operations.read method
 * if they use DRM events for asynchronous signalling to userspace.  Since
 * events are used by the KMS API for vblank and page flip completion this means
 * all modern display drivers must use it.
 *
 * See also drm_read().
 *
 * RETURNS:
 * Mask of POLL flags indicating the current status of the file.
 */
__poll_t drm_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct drm_file *file_priv = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &file_priv->event_wait, wait);

	if (!list_empty(&file_priv->event_list))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}
EXPORT_SYMBOL(drm_poll);
#endif

/**
 * drm_event_reserve_init_locked - init a DRM event and reserve space for it
 * @dev: DRM device
 * @file_priv: DRM file private data
 * @p: tracking structure for the pending event
 * @e: actual event data to deliver to userspace
 *
 * This function prepares the passed in event for eventual delivery. If the event
 * doesn't get delivered (because the IOCTL fails later on, before queuing up
 * anything) then the even must be cancelled and freed using
 * drm_event_cancel_free(). Successfully initialized events should be sent out
 * using drm_send_event() or drm_send_event_locked() to signal completion of the
 * asynchronous event to userspace.
 *
 * If callers embedded @p into a larger structure it must be allocated with
 * kmalloc and @p must be the first member element.
 *
 * This is the locked version of drm_event_reserve_init() for callers which
 * already hold &drm_device.event_lock.
 *
 * RETURNS:
 * 0 on success or a negative error code on failure.
 */
int drm_event_reserve_init_locked(struct drm_device *dev,
				  struct drm_file *file_priv,
				  struct drm_pending_event *p,
				  struct drm_event *e)
{
	if (file_priv->event_space < e->length)
		return -ENOMEM;

	file_priv->event_space -= e->length;

	p->event = e;
	list_add(&p->pending_link, &file_priv->pending_event_list);
	p->file_priv = file_priv;

	return 0;
}
EXPORT_SYMBOL(drm_event_reserve_init_locked);

/**
 * drm_event_reserve_init - init a DRM event and reserve space for it
 * @dev: DRM device
 * @file_priv: DRM file private data
 * @p: tracking structure for the pending event
 * @e: actual event data to deliver to userspace
 *
 * This function prepares the passed in event for eventual delivery. If the event
 * doesn't get delivered (because the IOCTL fails later on, before queuing up
 * anything) then the even must be cancelled and freed using
 * drm_event_cancel_free(). Successfully initialized events should be sent out
 * using drm_send_event() or drm_send_event_locked() to signal completion of the
 * asynchronous event to userspace.
 *
 * If callers embedded @p into a larger structure it must be allocated with
 * kmalloc and @p must be the first member element.
 *
 * Callers which already hold &drm_device.event_lock should use
 * drm_event_reserve_init_locked() instead.
 *
 * RETURNS:
 * 0 on success or a negative error code on failure.
 */
int drm_event_reserve_init(struct drm_device *dev,
			   struct drm_file *file_priv,
			   struct drm_pending_event *p,
			   struct drm_event *e)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev->event_lock, flags);
	ret = drm_event_reserve_init_locked(dev, file_priv, p, e);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return ret;
}
EXPORT_SYMBOL(drm_event_reserve_init);

/**
 * drm_event_cancel_free - free a DRM event and release its space
 * @dev: DRM device
 * @p: tracking structure for the pending event
 *
 * This function frees the event @p initialized with drm_event_reserve_init()
 * and releases any allocated space. It is used to cancel an event when the
 * nonblocking operation could not be submitted and needed to be aborted.
 */
void drm_event_cancel_free(struct drm_device *dev,
			   struct drm_pending_event *p)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (p->file_priv) {
		p->file_priv->event_space += p->event->length;
		list_del(&p->pending_link);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (p->fence)
		dma_fence_put(p->fence);

	kfree(p);
}
EXPORT_SYMBOL(drm_event_cancel_free);

static void drm_send_event_helper(struct drm_device *dev,
			   struct drm_pending_event *e, ktime_t timestamp)
{
	assert_spin_locked(&dev->event_lock);

	if (e->completion) {
		complete_all(e->completion);
		e->completion_release(e->completion);
		e->completion = NULL;
	}

	if (e->fence) {
		if (timestamp)
			dma_fence_signal_timestamp(e->fence, timestamp);
		else
			dma_fence_signal(e->fence);
		dma_fence_put(e->fence);
	}

	if (!e->file_priv) {
		kfree(e);
		return;
	}

	list_del(&e->pending_link);
	list_add_tail(&e->link,
		      &e->file_priv->event_list);
	wake_up_interruptible_poll(&e->file_priv->event_wait,
		EPOLLIN | EPOLLRDNORM);
#ifdef __OpenBSD__
	selwakeup(&e->file_priv->rsel);
#endif
}

/**
 * drm_send_event_timestamp_locked - send DRM event to file descriptor
 * @dev: DRM device
 * @e: DRM event to deliver
 * @timestamp: timestamp to set for the fence event in kernel's CLOCK_MONOTONIC
 * time domain
 *
 * This function sends the event @e, initialized with drm_event_reserve_init(),
 * to its associated userspace DRM file. Callers must already hold
 * &drm_device.event_lock.
 *
 * Note that the core will take care of unlinking and disarming events when the
 * corresponding DRM file is closed. Drivers need not worry about whether the
 * DRM file for this event still exists and can call this function upon
 * completion of the asynchronous work unconditionally.
 */
void drm_send_event_timestamp_locked(struct drm_device *dev,
				     struct drm_pending_event *e, ktime_t timestamp)
{
	drm_send_event_helper(dev, e, timestamp);
}
EXPORT_SYMBOL(drm_send_event_timestamp_locked);

/**
 * drm_send_event_locked - send DRM event to file descriptor
 * @dev: DRM device
 * @e: DRM event to deliver
 *
 * This function sends the event @e, initialized with drm_event_reserve_init(),
 * to its associated userspace DRM file. Callers must already hold
 * &drm_device.event_lock, see drm_send_event() for the unlocked version.
 *
 * Note that the core will take care of unlinking and disarming events when the
 * corresponding DRM file is closed. Drivers need not worry about whether the
 * DRM file for this event still exists and can call this function upon
 * completion of the asynchronous work unconditionally.
 */
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e)
{
	drm_send_event_helper(dev, e, 0);
}
EXPORT_SYMBOL(drm_send_event_locked);

/**
 * drm_send_event - send DRM event to file descriptor
 * @dev: DRM device
 * @e: DRM event to deliver
 *
 * This function sends the event @e, initialized with drm_event_reserve_init(),
 * to its associated userspace DRM file. This function acquires
 * &drm_device.event_lock, see drm_send_event_locked() for callers which already
 * hold this lock.
 *
 * Note that the core will take care of unlinking and disarming events when the
 * corresponding DRM file is closed. Drivers need not worry about whether the
 * DRM file for this event still exists and can call this function upon
 * completion of the asynchronous work unconditionally.
 */
void drm_send_event(struct drm_device *dev, struct drm_pending_event *e)
{
	unsigned long irqflags;

	spin_lock_irqsave(&dev->event_lock, irqflags);
	drm_send_event_helper(dev, e, 0);
	spin_unlock_irqrestore(&dev->event_lock, irqflags);
}
EXPORT_SYMBOL(drm_send_event);

static void print_size(struct drm_printer *p, const char *stat,
		       const char *region, u64 sz)
{
	const char *units[] = {"", " KiB", " MiB"};
	unsigned u;

	for (u = 0; u < ARRAY_SIZE(units) - 1; u++) {
		if (sz == 0 || !IS_ALIGNED(sz, SZ_1K))
			break;
		sz = div_u64(sz, SZ_1K);
	}

	drm_printf(p, "drm-%s-%s:\t%llu%s\n", stat, region, sz, units[u]);
}

/**
 * drm_print_memory_stats - A helper to print memory stats
 * @p: The printer to print output to
 * @stats: The collected memory stats
 * @supported_status: Bitmask of optional stats which are available
 * @region: The memory region
 *
 */
void drm_print_memory_stats(struct drm_printer *p,
			    const struct drm_memory_stats *stats,
			    enum drm_gem_object_status supported_status,
			    const char *region)
{
	print_size(p, "total", region, stats->private + stats->shared);
	print_size(p, "shared", region, stats->shared);
	print_size(p, "active", region, stats->active);

	if (supported_status & DRM_GEM_OBJECT_RESIDENT)
		print_size(p, "resident", region, stats->resident);

	if (supported_status & DRM_GEM_OBJECT_PURGEABLE)
		print_size(p, "purgeable", region, stats->purgeable);
}
EXPORT_SYMBOL(drm_print_memory_stats);

/**
 * drm_show_memory_stats - Helper to collect and show standard fdinfo memory stats
 * @p: the printer to print output to
 * @file: the DRM file
 *
 * Helper to iterate over GEM objects with a handle allocated in the specified
 * file.
 */
void drm_show_memory_stats(struct drm_printer *p, struct drm_file *file)
{
	struct drm_gem_object *obj;
	struct drm_memory_stats status = {};
	enum drm_gem_object_status supported_status = 0;
	int id;

	spin_lock(&file->table_lock);
	idr_for_each_entry (&file->object_idr, obj, id) {
		enum drm_gem_object_status s = 0;
		size_t add_size = (obj->funcs && obj->funcs->rss) ?
			obj->funcs->rss(obj) : obj->size;

		if (obj->funcs && obj->funcs->status) {
			s = obj->funcs->status(obj);
			supported_status = DRM_GEM_OBJECT_RESIDENT |
					DRM_GEM_OBJECT_PURGEABLE;
		}

		if (drm_gem_object_is_shared_for_memory_stats(obj)) {
			status.shared += obj->size;
		} else {
			status.private += obj->size;
		}

		if (s & DRM_GEM_OBJECT_RESIDENT) {
			status.resident += add_size;
		} else {
			/* If already purged or not yet backed by pages, don't
			 * count it as purgeable:
			 */
			s &= ~DRM_GEM_OBJECT_PURGEABLE;
		}

		if (!dma_resv_test_signaled(obj->resv, dma_resv_usage_rw(true))) {
			status.active += add_size;

			/* If still active, don't count as purgeable: */
			s &= ~DRM_GEM_OBJECT_PURGEABLE;
		}

		if (s & DRM_GEM_OBJECT_PURGEABLE)
			status.purgeable += add_size;
	}
	spin_unlock(&file->table_lock);

	drm_print_memory_stats(p, &status, supported_status, "memory");
}
EXPORT_SYMBOL(drm_show_memory_stats);

/**
 * drm_show_fdinfo - helper for drm file fops
 * @m: output stream
 * @f: the device file instance
 *
 * Helper to implement fdinfo, for userspace to query usage stats, etc, of a
 * process using the GPU.  See also &drm_driver.show_fdinfo.
 *
 * For text output format description please see Documentation/gpu/drm-usage-stats.rst
 */
void drm_show_fdinfo(struct seq_file *m, struct file *f)
{
	STUB();
#ifdef notyet
	struct drm_file *file = f->private_data;
	struct drm_device *dev = file->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return;

	drm_printf(&p, "drm-driver:\t%s\n", dev->driver->name);
	drm_printf(&p, "drm-client-id:\t%llu\n", file->client_id);

	if (dev_is_pci(dev->dev)) {
		struct pci_dev *pdev = to_pci_dev(dev->dev);

		drm_printf(&p, "drm-pdev:\t%04x:%02x:%02x.%d\n",
			   pci_domain_nr(pdev->bus), pdev->bus->number,
			   PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
	}

	if (dev->driver->show_fdinfo)
		dev->driver->show_fdinfo(&p, file);

	drm_dev_exit(idx);
#endif
}
EXPORT_SYMBOL(drm_show_fdinfo);

/**
 * mock_drm_getfile - Create a new struct file for the drm device
 * @minor: drm minor to wrap (e.g. #drm_device.primary)
 * @flags: file creation mode (O_RDWR etc)
 *
 * This create a new struct file that wraps a DRM file context around a
 * DRM minor. This mimicks userspace opening e.g. /dev/dri/card0, but without
 * invoking userspace. The struct file may be operated on using its f_op
 * (the drm_device.driver.fops) to mimick userspace operations, or be supplied
 * to userspace facing functions as an internal/anonymous client.
 *
 * RETURNS:
 * Pointer to newly created struct file, ERR_PTR on failure.
 */
struct file *mock_drm_getfile(struct drm_minor *minor, unsigned int flags)
{
	STUB();
	return ERR_PTR(-ENOSYS);
#ifdef notyet
	struct drm_device *dev = minor->dev;
	struct drm_file *priv;
	struct file *file;

	priv = drm_file_alloc(minor);
	if (IS_ERR(priv))
		return ERR_CAST(priv);

	file = anon_inode_getfile("drm", dev->driver->fops, priv, flags);
	if (IS_ERR(file)) {
		drm_file_free(priv);
		return file;
	}

	/* Everyone shares a single global address space */
	file->f_mapping = dev->anon_inode->i_mapping;

	drm_dev_get(dev);
	priv->filp = file;

	return file;
#endif
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(mock_drm_getfile);
