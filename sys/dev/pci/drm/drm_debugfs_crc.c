/*
 * Copyright © 2008 Intel Corporation
 * Copyright © 2016 Collabora Ltd
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Based on code from the i915 driver.
 * Original author: Damien Lespiau <damien.lespiau@intel.com>
 *
 */

#include <linux/circ_buf.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/poll.h>
#include <linux/uaccess.h>

#include <drm/drm_crtc.h>
#include <drm/drm_debugfs_crc.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>

#include "drm_internal.h"

/**
 * DOC: CRC ABI
 *
 * DRM device drivers can provide to userspace CRC information of each frame as
 * it reached a given hardware component (a CRC sampling "source").
 *
 * Userspace can control generation of CRCs in a given CRTC by writing to the
 * file dri/0/crtc-N/crc/control in debugfs, with N being the :ref:`index of
 * the CRTC<crtc_index>`. Accepted values are source names (which are
 * driver-specific) and the "auto" keyword, which will let the driver select a
 * default source of frame CRCs for this CRTC.
 *
 * Once frame CRC generation is enabled, userspace can capture them by reading
 * the dri/0/crtc-N/crc/data file. Each line in that file contains the frame
 * number in the first field and then a number of unsigned integer fields
 * containing the CRC data. Fields are separated by a single space and the number
 * of CRC fields is source-specific.
 *
 * Note that though in some cases the CRC is computed in a specified way and on
 * the frame contents as supplied by userspace (eDP 1.3), in general the CRC
 * computation is performed in an unspecified way and on frame contents that have
 * been already processed in also an unspecified way and thus userspace cannot
 * rely on being able to generate matching CRC values for the frame contents that
 * it submits. In this general case, the maximum userspace can do is to compare
 * the reported CRCs of frames that should have the same contents.
 *
 * On the driver side the implementation effort is minimal, drivers only need to
 * implement &drm_crtc_funcs.set_crc_source and &drm_crtc_funcs.verify_crc_source.
 * The debugfs files are automatically set up if those vfuncs are set. CRC samples
 * need to be captured in the driver by calling drm_crtc_add_crc_entry().
 * Depending on the driver and HW requirements, &drm_crtc_funcs.set_crc_source
 * may result in a commit (even a full modeset).
 *
 * CRC results must be reliable across non-full-modeset atomic commits, so if a
 * commit via DRM_IOCTL_MODE_ATOMIC would disable or otherwise interfere with
 * CRC generation, then the driver must mark that commit as a full modeset
 * (drm_atomic_crtc_needs_modeset() should return true). As a result, to ensure
 * consistent results, generic userspace must re-setup CRC generation after a
 * legacy SETCRTC or an atomic commit with DRM_MODE_ATOMIC_ALLOW_MODESET.
 */

static int crc_control_show(struct seq_file *m, void *data)
{
	struct drm_crtc *crtc = m->private;

	if (crtc->funcs->get_crc_sources) {
		size_t count;
		const char *const *sources = crtc->funcs->get_crc_sources(crtc,
									&count);
		size_t values_cnt;
		int i;

		if (count == 0 || !sources)
			goto out;

		for (i = 0; i < count; i++)
			if (!crtc->funcs->verify_crc_source(crtc, sources[i],
							    &values_cnt)) {
				if (strcmp(sources[i], crtc->crc.source))
					seq_printf(m, "%s\n", sources[i]);
				else
					seq_printf(m, "%s*\n", sources[i]);
			}
	}
	return 0;

out:
	seq_printf(m, "%s*\n", crtc->crc.source);
	return 0;
}

static int crc_control_open(struct inode *inode, struct file *file)
{
	struct drm_crtc *crtc = inode->i_private;

	return single_open(file, crc_control_show, crtc);
}

static ssize_t crc_control_write(struct file *file, const char __user *ubuf,
				 size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_crtc *crtc = m->private;
	struct drm_crtc_crc *crc = &crtc->crc;
	char *source;
	size_t values_cnt;
	int ret;

	if (len == 0)
		return 0;

	if (len > PAGE_SIZE - 1) {
		DRM_DEBUG_KMS("Expected < %lu bytes into crtc crc control\n",
			      PAGE_SIZE);
		return -E2BIG;
	}

	source = memdup_user_nul(ubuf, len);
	if (IS_ERR(source))
		return PTR_ERR(source);

	if (source[len - 1] == '\n')
		source[len - 1] = '\0';

	ret = crtc->funcs->verify_crc_source(crtc, source, &values_cnt);
	if (ret) {
		kfree(source);
		return ret;
	}

	spin_lock_irq(&crc->lock);

	if (crc->opened) {
		spin_unlock_irq(&crc->lock);
		kfree(source);
		return -EBUSY;
	}

	kfree(crc->source);
	crc->source = source;

	spin_unlock_irq(&crc->lock);

	*offp += len;
	return len;
}

static const struct file_operations drm_crtc_crc_control_fops = {
	.owner = THIS_MODULE,
	.open = crc_control_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = crc_control_write
};

static int crtc_crc_data_count(struct drm_crtc_crc *crc)
{
	assert_spin_locked(&crc->lock);
	return CIRC_CNT(crc->head, crc->tail, DRM_CRC_ENTRIES_NR);
}

static void crtc_crc_cleanup(struct drm_crtc_crc *crc)
{
	kfree(crc->entries);
	crc->overflow = false;
	crc->entries = NULL;
	crc->head = 0;
	crc->tail = 0;
	crc->values_cnt = 0;
	crc->opened = false;
}

static int crtc_crc_open(struct inode *inode, struct file *filep)
{
	struct drm_crtc *crtc = inode->i_private;
	struct drm_crtc_crc *crc = &crtc->crc;
	struct drm_crtc_crc_entry *entries = NULL;
	size_t values_cnt;
	int ret = 0;

	if (drm_drv_uses_atomic_modeset(crtc->dev)) {
		ret = drm_modeset_lock_single_interruptible(&crtc->mutex);
		if (ret)
			return ret;

		if (!crtc->state->active)
			ret = -EIO;
		drm_modeset_unlock(&crtc->mutex);

		if (ret)
			return ret;
	}

	ret = crtc->funcs->verify_crc_source(crtc, crc->source, &values_cnt);
	if (ret)
		return ret;

	if (WARN_ON(values_cnt > DRM_MAX_CRC_NR))
		return -EINVAL;

	if (WARN_ON(values_cnt == 0))
		return -EINVAL;

	entries = kcalloc(DRM_CRC_ENTRIES_NR, sizeof(*entries), GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	spin_lock_irq(&crc->lock);
	if (!crc->opened) {
		crc->opened = true;
		crc->entries = entries;
		crc->values_cnt = values_cnt;
	} else {
		ret = -EBUSY;
	}
	spin_unlock_irq(&crc->lock);

	if (ret) {
		kfree(entries);
		return ret;
	}

	ret = crtc->funcs->set_crc_source(crtc, crc->source);
	if (ret)
		goto err;

	return 0;

err:
	spin_lock_irq(&crc->lock);
	crtc_crc_cleanup(crc);
	spin_unlock_irq(&crc->lock);
	return ret;
}

static int crtc_crc_release(struct inode *inode, struct file *filep)
{
	struct drm_crtc *crtc = filep->f_inode->i_private;
	struct drm_crtc_crc *crc = &crtc->crc;

	/* terminate the infinite while loop if 'drm_dp_aux_crc_work' running */
	spin_lock_irq(&crc->lock);
	crc->opened = false;
	spin_unlock_irq(&crc->lock);

	crtc->funcs->set_crc_source(crtc, NULL);

	spin_lock_irq(&crc->lock);
	crtc_crc_cleanup(crc);
	spin_unlock_irq(&crc->lock);

	return 0;
}

/*
 * 1 frame field of 10 chars plus a number of CRC fields of 10 chars each, space
 * separated, with a newline at the end and null-terminated.
 */
#define LINE_LEN(values_cnt)	(10 + 11 * values_cnt + 1 + 1)
#define MAX_LINE_LEN		(LINE_LEN(DRM_MAX_CRC_NR))

static ssize_t crtc_crc_read(struct file *filep, char __user *user_buf,
			     size_t count, loff_t *pos)
{
	struct drm_crtc *crtc = filep->f_inode->i_private;
	struct drm_crtc_crc *crc = &crtc->crc;
	struct drm_crtc_crc_entry *entry;
	char buf[MAX_LINE_LEN];
	int ret, i;

	spin_lock_irq(&crc->lock);

	if (!crc->source) {
		spin_unlock_irq(&crc->lock);
		return 0;
	}

	/* Nothing to read? */
	while (crtc_crc_data_count(crc) == 0) {
		if (filep->f_flags & O_NONBLOCK) {
			spin_unlock_irq(&crc->lock);
			return -EAGAIN;
		}

		ret = wait_event_interruptible_lock_irq(crc->wq,
							crtc_crc_data_count(crc),
							crc->lock);
		if (ret) {
			spin_unlock_irq(&crc->lock);
			return ret;
		}
	}

	/* We know we have an entry to be read */
	entry = &crc->entries[crc->tail];

	if (count < LINE_LEN(crc->values_cnt)) {
		spin_unlock_irq(&crc->lock);
		return -EINVAL;
	}

	BUILD_BUG_ON_NOT_POWER_OF_2(DRM_CRC_ENTRIES_NR);
	crc->tail = (crc->tail + 1) & (DRM_CRC_ENTRIES_NR - 1);

	spin_unlock_irq(&crc->lock);

	if (entry->has_frame_counter)
		sprintf(buf, "0x%08x", entry->frame);
	else
		sprintf(buf, "XXXXXXXXXX");

	for (i = 0; i < crc->values_cnt; i++)
		sprintf(buf + 10 + i * 11, " 0x%08x", entry->crcs[i]);
	sprintf(buf + 10 + crc->values_cnt * 11, "\n");

	if (copy_to_user(user_buf, buf, LINE_LEN(crc->values_cnt)))
		return -EFAULT;

	return LINE_LEN(crc->values_cnt);
}

static __poll_t crtc_crc_poll(struct file *file, poll_table *wait)
{
	struct drm_crtc *crtc = file->f_inode->i_private;
	struct drm_crtc_crc *crc = &crtc->crc;
	__poll_t ret = 0;

	poll_wait(file, &crc->wq, wait);

	spin_lock_irq(&crc->lock);
	if (crc->source && crtc_crc_data_count(crc))
		ret |= EPOLLIN | EPOLLRDNORM;
	spin_unlock_irq(&crc->lock);

	return ret;
}

static const struct file_operations drm_crtc_crc_data_fops = {
	.owner = THIS_MODULE,
	.open = crtc_crc_open,
	.read = crtc_crc_read,
	.poll = crtc_crc_poll,
	.release = crtc_crc_release,
};

void drm_debugfs_crtc_crc_add(struct drm_crtc *crtc)
{
	struct dentry *crc_ent;

	if (!crtc->funcs->set_crc_source || !crtc->funcs->verify_crc_source)
		return;

	crc_ent = debugfs_create_dir("crc", crtc->debugfs_entry);

	debugfs_create_file("control", S_IRUGO | S_IWUSR, crc_ent, crtc,
			    &drm_crtc_crc_control_fops);
	debugfs_create_file("data", S_IRUGO, crc_ent, crtc,
			    &drm_crtc_crc_data_fops);
}

/**
 * drm_crtc_add_crc_entry - Add entry with CRC information for a frame
 * @crtc: CRTC to which the frame belongs
 * @has_frame: whether this entry has a frame number to go with
 * @frame: number of the frame these CRCs are about
 * @crcs: array of CRC values, with length matching #drm_crtc_crc.values_cnt
 *
 * For each frame, the driver polls the source of CRCs for new data and calls
 * this function to add them to the buffer from where userspace reads.
 */
int drm_crtc_add_crc_entry(struct drm_crtc *crtc, bool has_frame,
			   uint32_t frame, uint32_t *crcs)
{
	struct drm_crtc_crc *crc = &crtc->crc;
	struct drm_crtc_crc_entry *entry;
	int head, tail;
	unsigned long flags;

	spin_lock_irqsave(&crc->lock, flags);

	/* Caller may not have noticed yet that userspace has stopped reading */
	if (!crc->entries) {
		spin_unlock_irqrestore(&crc->lock, flags);
		return -EINVAL;
	}

	head = crc->head;
	tail = crc->tail;

	if (CIRC_SPACE(head, tail, DRM_CRC_ENTRIES_NR) < 1) {
		bool was_overflow = crc->overflow;

		crc->overflow = true;
		spin_unlock_irqrestore(&crc->lock, flags);

		if (!was_overflow)
			DRM_ERROR("Overflow of CRC buffer, userspace reads too slow.\n");

		return -ENOBUFS;
	}

	entry = &crc->entries[head];
	entry->frame = frame;
	entry->has_frame_counter = has_frame;
	memcpy(&entry->crcs, crcs, sizeof(*crcs) * crc->values_cnt);

	head = (head + 1) & (DRM_CRC_ENTRIES_NR - 1);
	crc->head = head;

	spin_unlock_irqrestore(&crc->lock, flags);

	wake_up_interruptible(&crc->wq);

	return 0;
}
EXPORT_SYMBOL_GPL(drm_crtc_add_crc_entry);
