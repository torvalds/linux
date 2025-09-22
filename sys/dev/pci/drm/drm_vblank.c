/*
 * drm_irq.c IRQ and vblank support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
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

#include <linux/export.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>

#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "drm_internal.h"
#include "drm_trace.h"

/**
 * DOC: vblank handling
 *
 * From the computer's perspective, every time the monitor displays
 * a new frame the scanout engine has "scanned out" the display image
 * from top to bottom, one row of pixels at a time. The current row
 * of pixels is referred to as the current scanline.
 *
 * In addition to the display's visible area, there's usually a couple of
 * extra scanlines which aren't actually displayed on the screen.
 * These extra scanlines don't contain image data and are occasionally used
 * for features like audio and infoframes. The region made up of these
 * scanlines is referred to as the vertical blanking region, or vblank for
 * short.
 *
 * For historical reference, the vertical blanking period was designed to
 * give the electron gun (on CRTs) enough time to move back to the top of
 * the screen to start scanning out the next frame. Similar for horizontal
 * blanking periods. They were designed to give the electron gun enough
 * time to move back to the other side of the screen to start scanning the
 * next scanline.
 *
 * ::
 *
 *
 *    physical →   ⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽
 *    top of      |                                        |
 *    display     |                                        |
 *                |               New frame                |
 *                |                                        |
 *                |↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓|
 *                |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| ← Scanline,
 *                |↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓|   updates the
 *                |                                        |   frame as it
 *                |                                        |   travels down
 *                |                                        |   ("scan out")
 *                |               Old frame                |
 *                |                                        |
 *                |                                        |
 *                |                                        |
 *                |                                        |   physical
 *                |                                        |   bottom of
 *    vertical    |⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽| ← display
 *    blanking    ┆xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx┆
 *    region   →  ┆xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx┆
 *                ┆xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx┆
 *    start of →   ⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽⎽
 *    new frame
 *
 * "Physical top of display" is the reference point for the high-precision/
 * corrected timestamp.
 *
 * On a lot of display hardware, programming needs to take effect during the
 * vertical blanking period so that settings like gamma, the image buffer
 * buffer to be scanned out, etc. can safely be changed without showing
 * any visual artifacts on the screen. In some unforgiving hardware, some of
 * this programming has to both start and end in the same vblank. To help
 * with the timing of the hardware programming, an interrupt is usually
 * available to notify the driver when it can start the updating of registers.
 * The interrupt is in this context named the vblank interrupt.
 *
 * The vblank interrupt may be fired at different points depending on the
 * hardware. Some hardware implementations will fire the interrupt when the
 * new frame start, other implementations will fire the interrupt at different
 * points in time.
 *
 * Vertical blanking plays a major role in graphics rendering. To achieve
 * tear-free display, users must synchronize page flips and/or rendering to
 * vertical blanking. The DRM API offers ioctls to perform page flips
 * synchronized to vertical blanking and wait for vertical blanking.
 *
 * The DRM core handles most of the vertical blanking management logic, which
 * involves filtering out spurious interrupts, keeping race-free blanking
 * counters, coping with counter wrap-around and resets and keeping use counts.
 * It relies on the driver to generate vertical blanking interrupts and
 * optionally provide a hardware vertical blanking counter.
 *
 * Drivers must initialize the vertical blanking handling core with a call to
 * drm_vblank_init(). Minimally, a driver needs to implement
 * &drm_crtc_funcs.enable_vblank and &drm_crtc_funcs.disable_vblank plus call
 * drm_crtc_handle_vblank() in its vblank interrupt handler for working vblank
 * support.
 *
 * Vertical blanking interrupts can be enabled by the DRM core or by drivers
 * themselves (for instance to handle page flipping operations).  The DRM core
 * maintains a vertical blanking use count to ensure that the interrupts are not
 * disabled while a user still needs them. To increment the use count, drivers
 * call drm_crtc_vblank_get() and release the vblank reference again with
 * drm_crtc_vblank_put(). In between these two calls vblank interrupts are
 * guaranteed to be enabled.
 *
 * On many hardware disabling the vblank interrupt cannot be done in a race-free
 * manner, see &drm_vblank_crtc_config.disable_immediate and
 * &drm_driver.max_vblank_count. In that case the vblank core only disables the
 * vblanks after a timer has expired, which can be configured through the
 * ``vblankoffdelay`` module parameter.
 *
 * Drivers for hardware without support for vertical-blanking interrupts
 * must not call drm_vblank_init(). For such drivers, atomic helpers will
 * automatically generate fake vblank events as part of the display update.
 * This functionality also can be controlled by the driver by enabling and
 * disabling struct drm_crtc_state.no_vblank.
 */

/* Retry timestamp calculation up to 3 times to satisfy
 * drm_timestamp_precision before giving up.
 */
#define DRM_TIMESTAMP_MAXRETRIES 3

/* Threshold in nanoseconds for detection of redundant
 * vblank irq in drm_handle_vblank(). 1 msec should be ok.
 */
#define DRM_REDUNDANT_VBLIRQ_THRESH_NS 1000000

static bool
drm_get_last_vbltimestamp(struct drm_device *dev, unsigned int pipe,
			  ktime_t *tvblank, bool in_vblank_irq);

static unsigned int drm_timestamp_precision = 20;  /* Default to 20 usecs. */

static int drm_vblank_offdelay = 5000;    /* Default to 5000 msecs. */

module_param_named(vblankoffdelay, drm_vblank_offdelay, int, 0600);
module_param_named(timestamp_precision_usec, drm_timestamp_precision, int, 0600);
MODULE_PARM_DESC(vblankoffdelay, "Delay until vblank irq auto-disable [msecs] (0: never disable, <0: disable immediately)");
MODULE_PARM_DESC(timestamp_precision_usec, "Max. error on timestamps [usecs]");

static struct drm_vblank_crtc *
drm_vblank_crtc(struct drm_device *dev, unsigned int pipe)
{
	return &dev->vblank[pipe];
}

struct drm_vblank_crtc *
drm_crtc_vblank_crtc(struct drm_crtc *crtc)
{
	return drm_vblank_crtc(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_crtc);

static void store_vblank(struct drm_device *dev, unsigned int pipe,
			 u32 vblank_count_inc,
			 ktime_t t_vblank, u32 last)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);

	assert_spin_locked(&dev->vblank_time_lock);

	vblank->last = last;

	write_seqlock(&vblank->seqlock);
	vblank->time = t_vblank;
	atomic64_add(vblank_count_inc, &vblank->count);
	write_sequnlock(&vblank->seqlock);
}

static u32 drm_max_vblank_count(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);

	return vblank->max_vblank_count ?: dev->max_vblank_count;
}

/*
 * "No hw counter" fallback implementation of .get_vblank_counter() hook,
 * if there is no usable hardware frame counter available.
 */
static u32 drm_vblank_no_hw_counter(struct drm_device *dev, unsigned int pipe)
{
	drm_WARN_ON_ONCE(dev, drm_max_vblank_count(dev, pipe) != 0);
	return 0;
}

static u32 __get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

		if (drm_WARN_ON(dev, !crtc))
			return 0;

		if (crtc->funcs->get_vblank_counter)
			return crtc->funcs->get_vblank_counter(crtc);
	}

	return drm_vblank_no_hw_counter(dev, pipe);
}

/*
 * Reset the stored timestamp for the current vblank count to correspond
 * to the last vblank occurred.
 *
 * Only to be called from drm_crtc_vblank_on().
 *
 * Note: caller must hold &drm_device.vbl_lock since this reads & writes
 * device vblank fields.
 */
static void drm_reset_vblank_timestamp(struct drm_device *dev, unsigned int pipe)
{
	u32 cur_vblank;
	bool rc;
	ktime_t t_vblank;
	int count = DRM_TIMESTAMP_MAXRETRIES;

	spin_lock(&dev->vblank_time_lock);

	/*
	 * sample the current counter to avoid random jumps
	 * when drm_vblank_enable() applies the diff
	 */
	do {
		cur_vblank = __get_vblank_counter(dev, pipe);
		rc = drm_get_last_vbltimestamp(dev, pipe, &t_vblank, false);
	} while (cur_vblank != __get_vblank_counter(dev, pipe) && --count > 0);

	/*
	 * Only reinitialize corresponding vblank timestamp if high-precision query
	 * available and didn't fail. Otherwise reinitialize delayed at next vblank
	 * interrupt and assign 0 for now, to mark the vblanktimestamp as invalid.
	 */
	if (!rc)
		t_vblank = 0;

	/*
	 * +1 to make sure user will never see the same
	 * vblank counter value before and after a modeset
	 */
	store_vblank(dev, pipe, 1, t_vblank, cur_vblank);

	spin_unlock(&dev->vblank_time_lock);
}

/*
 * Call back into the driver to update the appropriate vblank counter
 * (specified by @pipe).  Deal with wraparound, if it occurred, and
 * update the last read value so we can deal with wraparound on the next
 * call if necessary.
 *
 * Only necessary when going from off->on, to account for frames we
 * didn't get an interrupt for.
 *
 * Note: caller must hold &drm_device.vbl_lock since this reads & writes
 * device vblank fields.
 */
static void drm_update_vblank_count(struct drm_device *dev, unsigned int pipe,
				    bool in_vblank_irq)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	u32 cur_vblank, diff;
	bool rc;
	ktime_t t_vblank;
	int count = DRM_TIMESTAMP_MAXRETRIES;
	int framedur_ns = vblank->framedur_ns;
	u32 max_vblank_count = drm_max_vblank_count(dev, pipe);

	/*
	 * Interrupts were disabled prior to this call, so deal with counter
	 * wrap if needed.
	 * NOTE!  It's possible we lost a full dev->max_vblank_count + 1 events
	 * here if the register is small or we had vblank interrupts off for
	 * a long time.
	 *
	 * We repeat the hardware vblank counter & timestamp query until
	 * we get consistent results. This to prevent races between gpu
	 * updating its hardware counter while we are retrieving the
	 * corresponding vblank timestamp.
	 */
	do {
		cur_vblank = __get_vblank_counter(dev, pipe);
		rc = drm_get_last_vbltimestamp(dev, pipe, &t_vblank, in_vblank_irq);
	} while (cur_vblank != __get_vblank_counter(dev, pipe) && --count > 0);

	if (max_vblank_count) {
		/* trust the hw counter when it's around */
		diff = (cur_vblank - vblank->last) & max_vblank_count;
	} else if (rc && framedur_ns) {
		u64 diff_ns = ktime_to_ns(ktime_sub(t_vblank, vblank->time));

		/*
		 * Figure out how many vblanks we've missed based
		 * on the difference in the timestamps and the
		 * frame/field duration.
		 */

		drm_dbg_vbl(dev, "crtc %u: Calculating number of vblanks."
			    " diff_ns = %lld, framedur_ns = %d)\n",
			    pipe, (long long)diff_ns, framedur_ns);

		diff = DIV_ROUND_CLOSEST_ULL(diff_ns, framedur_ns);

		if (diff == 0 && in_vblank_irq)
			drm_dbg_vbl(dev, "crtc %u: Redundant vblirq ignored\n",
				    pipe);
	} else {
		/* some kind of default for drivers w/o accurate vbl timestamping */
		diff = in_vblank_irq ? 1 : 0;
	}

	/*
	 * Within a drm_vblank_pre_modeset - drm_vblank_post_modeset
	 * interval? If so then vblank irqs keep running and it will likely
	 * happen that the hardware vblank counter is not trustworthy as it
	 * might reset at some point in that interval and vblank timestamps
	 * are not trustworthy either in that interval. Iow. this can result
	 * in a bogus diff >> 1 which must be avoided as it would cause
	 * random large forward jumps of the software vblank counter.
	 */
	if (diff > 1 && (vblank->inmodeset & 0x2)) {
		drm_dbg_vbl(dev,
			    "clamping vblank bump to 1 on crtc %u: diffr=%u"
			    " due to pre-modeset.\n", pipe, diff);
		diff = 1;
	}

	drm_dbg_vbl(dev, "updating vblank count on crtc %u:"
		    " current=%llu, diff=%u, hw=%u hw_last=%u\n",
		    pipe, (unsigned long long)atomic64_read(&vblank->count),
		    diff, cur_vblank, vblank->last);

	if (diff == 0) {
		drm_WARN_ON_ONCE(dev, cur_vblank != vblank->last);
		return;
	}

	/*
	 * Only reinitialize corresponding vblank timestamp if high-precision query
	 * available and didn't fail, or we were called from the vblank interrupt.
	 * Otherwise reinitialize delayed at next vblank interrupt and assign 0
	 * for now, to mark the vblanktimestamp as invalid.
	 */
	if (!rc && !in_vblank_irq)
		t_vblank = 0;

	store_vblank(dev, pipe, diff, t_vblank, cur_vblank);
}

u64 drm_vblank_count(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	u64 count;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return 0;

	count = atomic64_read(&vblank->count);

	/*
	 * This read barrier corresponds to the implicit write barrier of the
	 * write seqlock in store_vblank(). Note that this is the only place
	 * where we need an explicit barrier, since all other access goes
	 * through drm_vblank_count_and_time(), which already has the required
	 * read barrier curtesy of the read seqlock.
	 */
	smp_rmb();

	return count;
}

/**
 * drm_crtc_accurate_vblank_count - retrieve the master vblank counter
 * @crtc: which counter to retrieve
 *
 * This function is similar to drm_crtc_vblank_count() but this function
 * interpolates to handle a race with vblank interrupts using the high precision
 * timestamping support.
 *
 * This is mostly useful for hardware that can obtain the scanout position, but
 * doesn't have a hardware frame counter.
 */
u64 drm_crtc_accurate_vblank_count(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	u64 vblank;
	unsigned long flags;

	drm_WARN_ONCE(dev, drm_debug_enabled(DRM_UT_VBL) &&
		      !crtc->funcs->get_vblank_timestamp,
		      "This function requires support for accurate vblank timestamps.");

	spin_lock_irqsave(&dev->vblank_time_lock, flags);

	drm_update_vblank_count(dev, pipe, false);
	vblank = drm_vblank_count(dev, pipe);

	spin_unlock_irqrestore(&dev->vblank_time_lock, flags);

	return vblank;
}
EXPORT_SYMBOL(drm_crtc_accurate_vblank_count);

static void __disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

		if (drm_WARN_ON(dev, !crtc))
			return;

		if (crtc->funcs->disable_vblank)
			crtc->funcs->disable_vblank(crtc);
	}
}

/*
 * Disable vblank irq's on crtc, make sure that last vblank count
 * of hardware and corresponding consistent software vblank counter
 * are preserved, even if there are any spurious vblank irq's after
 * disable.
 */
void drm_vblank_disable_and_save(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	unsigned long irqflags;

	assert_spin_locked(&dev->vbl_lock);

	/* Prevent vblank irq processing while disabling vblank irqs,
	 * so no updates of timestamps or count can happen after we've
	 * disabled. Needed to prevent races in case of delayed irq's.
	 */
	spin_lock_irqsave(&dev->vblank_time_lock, irqflags);

	/*
	 * Update vblank count and disable vblank interrupts only if the
	 * interrupts were enabled. This avoids calling the ->disable_vblank()
	 * operation in atomic context with the hardware potentially runtime
	 * suspended.
	 */
	if (!vblank->enabled)
		goto out;

	/*
	 * Update the count and timestamp to maintain the
	 * appearance that the counter has been ticking all along until
	 * this time. This makes the count account for the entire time
	 * between drm_crtc_vblank_on() and drm_crtc_vblank_off().
	 */
	drm_update_vblank_count(dev, pipe, false);
	__disable_vblank(dev, pipe);
	vblank->enabled = false;

out:
	spin_unlock_irqrestore(&dev->vblank_time_lock, irqflags);
}

static void vblank_disable_fn(void *arg)
{
	struct drm_vblank_crtc *vblank = arg;
	struct drm_device *dev = vblank->dev;
	unsigned int pipe = vblank->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	if (atomic_read(&vblank->refcount) == 0 && vblank->enabled) {
		drm_dbg_core(dev, "disabling vblank on crtc %u\n", pipe);
		drm_vblank_disable_and_save(dev, pipe);
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
}

static void drm_vblank_init_release(struct drm_device *dev, void *ptr)
{
	struct drm_vblank_crtc *vblank = ptr;

	drm_WARN_ON(dev, READ_ONCE(vblank->enabled) &&
		    drm_core_check_feature(dev, DRIVER_MODESET));

	drm_vblank_destroy_worker(vblank);
	del_timer_sync(&vblank->disable_timer);
}

/**
 * drm_vblank_init - initialize vblank support
 * @dev: DRM device
 * @num_crtcs: number of CRTCs supported by @dev
 *
 * This function initializes vblank support for @num_crtcs display pipelines.
 * Cleanup is handled automatically through a cleanup function added with
 * drmm_add_action_or_reset().
 *
 * Returns:
 * Zero on success or a negative error code on failure.
 */
int drm_vblank_init(struct drm_device *dev, unsigned int num_crtcs)
{
	int ret;
	unsigned int i;

	mtx_init(&dev->vbl_lock, IPL_TTY);
	mtx_init(&dev->vblank_time_lock, IPL_TTY);

	dev->vblank = drmm_kcalloc(dev, num_crtcs, sizeof(*dev->vblank), GFP_KERNEL);
	if (!dev->vblank)
		return -ENOMEM;

	dev->num_crtcs = num_crtcs;

	for (i = 0; i < num_crtcs; i++) {
		struct drm_vblank_crtc *vblank = &dev->vblank[i];

		vblank->dev = dev;
		vblank->pipe = i;
		init_waitqueue_head(&vblank->queue);
#ifdef __linux__
		timer_setup(&vblank->disable_timer, vblank_disable_fn, 0);
#else
		timeout_set(&vblank->disable_timer, vblank_disable_fn, vblank);
#endif
		seqlock_init(&vblank->seqlock, IPL_TTY);

		ret = drmm_add_action_or_reset(dev, drm_vblank_init_release,
					       vblank);
		if (ret)
			return ret;

		ret = drm_vblank_worker_init(vblank);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_vblank_init);

/**
 * drm_dev_has_vblank - test if vblanking has been initialized for
 *                      a device
 * @dev: the device
 *
 * Drivers may call this function to test if vblank support is
 * initialized for a device. For most hardware this means that vblanking
 * can also be enabled.
 *
 * Atomic helpers use this function to initialize
 * &drm_crtc_state.no_vblank. See also drm_atomic_helper_check_modeset().
 *
 * Returns:
 * True if vblanking has been initialized for the given device, false
 * otherwise.
 */
bool drm_dev_has_vblank(const struct drm_device *dev)
{
	return dev->num_crtcs != 0;
}
EXPORT_SYMBOL(drm_dev_has_vblank);

/**
 * drm_crtc_vblank_waitqueue - get vblank waitqueue for the CRTC
 * @crtc: which CRTC's vblank waitqueue to retrieve
 *
 * This function returns a pointer to the vblank waitqueue for the CRTC.
 * Drivers can use this to implement vblank waits using wait_event() and related
 * functions.
 */
wait_queue_head_t *drm_crtc_vblank_waitqueue(struct drm_crtc *crtc)
{
	return &crtc->dev->vblank[drm_crtc_index(crtc)].queue;
}
EXPORT_SYMBOL(drm_crtc_vblank_waitqueue);


/**
 * drm_calc_timestamping_constants - calculate vblank timestamp constants
 * @crtc: drm_crtc whose timestamp constants should be updated.
 * @mode: display mode containing the scanout timings
 *
 * Calculate and store various constants which are later needed by vblank and
 * swap-completion timestamping, e.g, by
 * drm_crtc_vblank_helper_get_vblank_timestamp(). They are derived from
 * CRTC's true scanout timing, so they take things like panel scaling or
 * other adjustments into account.
 */
void drm_calc_timestamping_constants(struct drm_crtc *crtc,
				     const struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(crtc);
	int linedur_ns = 0, framedur_ns = 0;
	int dotclock = mode->crtc_clock;

	if (!drm_dev_has_vblank(dev))
		return;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return;

	/* Valid dotclock? */
	if (dotclock > 0) {
		int frame_size = mode->crtc_htotal * mode->crtc_vtotal;

		/*
		 * Convert scanline length in pixels and video
		 * dot clock to line duration and frame duration
		 * in nanoseconds:
		 */
		linedur_ns  = div_u64((u64) mode->crtc_htotal * 1000000, dotclock);
		framedur_ns = div_u64((u64) frame_size * 1000000, dotclock);

		/*
		 * Fields of interlaced scanout modes are only half a frame duration.
		 */
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			framedur_ns /= 2;
	} else {
		drm_err(dev, "crtc %u: Can't calculate constants, dotclock = 0!\n",
			crtc->base.id);
	}

	vblank->linedur_ns  = linedur_ns;
	vblank->framedur_ns = framedur_ns;
	drm_mode_copy(&vblank->hwmode, mode);

	drm_dbg_core(dev,
		     "crtc %u: hwmode: htotal %d, vtotal %d, vdisplay %d\n",
		     crtc->base.id, mode->crtc_htotal,
		     mode->crtc_vtotal, mode->crtc_vdisplay);
	drm_dbg_core(dev, "crtc %u: clock %d kHz framedur %d linedur %d\n",
		     crtc->base.id, dotclock, framedur_ns, linedur_ns);
}
EXPORT_SYMBOL(drm_calc_timestamping_constants);

/**
 * drm_crtc_vblank_helper_get_vblank_timestamp_internal - precise vblank
 *                                                        timestamp helper
 * @crtc: CRTC whose vblank timestamp to retrieve
 * @max_error: Desired maximum allowable error in timestamps (nanosecs)
 *             On return contains true maximum error of timestamp
 * @vblank_time: Pointer to time which should receive the timestamp
 * @in_vblank_irq:
 *     True when called from drm_crtc_handle_vblank().  Some drivers
 *     need to apply some workarounds for gpu-specific vblank irq quirks
 *     if flag is set.
 * @get_scanout_position:
 *     Callback function to retrieve the scanout position. See
 *     @struct drm_crtc_helper_funcs.get_scanout_position.
 *
 * Implements calculation of exact vblank timestamps from given drm_display_mode
 * timings and current video scanout position of a CRTC.
 *
 * The current implementation only handles standard video modes. For double scan
 * and interlaced modes the driver is supposed to adjust the hardware mode
 * (taken from &drm_crtc_state.adjusted mode for atomic modeset drivers) to
 * match the scanout position reported.
 *
 * Note that atomic drivers must call drm_calc_timestamping_constants() before
 * enabling a CRTC. The atomic helpers already take care of that in
 * drm_atomic_helper_calc_timestamping_constants().
 *
 * Returns:
 * Returns true on success, and false on failure, i.e. when no accurate
 * timestamp could be acquired.
 */
bool
drm_crtc_vblank_helper_get_vblank_timestamp_internal(
	struct drm_crtc *crtc, int *max_error, ktime_t *vblank_time,
	bool in_vblank_irq,
	drm_vblank_get_scanout_position_func get_scanout_position)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	struct timespec64 ts_etime, ts_vblank_time;
	ktime_t stime, etime;
	bool vbl_status;
	const struct drm_display_mode *mode;
	int vpos, hpos, i;
	int delta_ns, duration_ns;

	if (pipe >= dev->num_crtcs) {
		drm_err(dev, "Invalid crtc %u\n", pipe);
		return false;
	}

	/* Scanout position query not supported? Should not happen. */
	if (!get_scanout_position) {
		drm_err(dev, "Called from CRTC w/o get_scanout_position()!?\n");
		return false;
	}

	if (drm_drv_uses_atomic_modeset(dev))
		mode = &vblank->hwmode;
	else
		mode = &crtc->hwmode;

	/* If mode timing undefined, just return as no-op:
	 * Happens during initial modesetting of a crtc.
	 */
	if (mode->crtc_clock == 0) {
		drm_dbg_core(dev, "crtc %u: Noop due to uninitialized mode.\n",
			     pipe);
		drm_WARN_ON_ONCE(dev, drm_drv_uses_atomic_modeset(dev));
		return false;
	}

	/* Get current scanout position with system timestamp.
	 * Repeat query up to DRM_TIMESTAMP_MAXRETRIES times
	 * if single query takes longer than max_error nanoseconds.
	 *
	 * This guarantees a tight bound on maximum error if
	 * code gets preempted or delayed for some reason.
	 */
	for (i = 0; i < DRM_TIMESTAMP_MAXRETRIES; i++) {
		/*
		 * Get vertical and horizontal scanout position vpos, hpos,
		 * and bounding timestamps stime, etime, pre/post query.
		 */
		vbl_status = get_scanout_position(crtc, in_vblank_irq,
						  &vpos, &hpos,
						  &stime, &etime,
						  mode);

		/* Return as no-op if scanout query unsupported or failed. */
		if (!vbl_status) {
			drm_dbg_core(dev,
				     "crtc %u : scanoutpos query failed.\n",
				     pipe);
			return false;
		}

		/* Compute uncertainty in timestamp of scanout position query. */
		duration_ns = ktime_to_ns(etime) - ktime_to_ns(stime);

		/* Accept result with <  max_error nsecs timing uncertainty. */
		if (duration_ns <= *max_error)
			break;
	}

	/* Noisy system timing? */
	if (i == DRM_TIMESTAMP_MAXRETRIES) {
		drm_dbg_core(dev,
			     "crtc %u: Noisy timestamp %d us > %d us [%d reps].\n",
			     pipe, duration_ns / 1000, *max_error / 1000, i);
	}

	/* Return upper bound of timestamp precision error. */
	*max_error = duration_ns;

	/* Convert scanout position into elapsed time at raw_time query
	 * since start of scanout at first display scanline. delta_ns
	 * can be negative if start of scanout hasn't happened yet.
	 */
	delta_ns = div_s64(1000000LL * (vpos * mode->crtc_htotal + hpos),
			   mode->crtc_clock);

	/* Subtract time delta from raw timestamp to get final
	 * vblank_time timestamp for end of vblank.
	 */
	*vblank_time = ktime_sub_ns(etime, delta_ns);

	if (!drm_debug_enabled(DRM_UT_VBL))
		return true;

	ts_etime = ktime_to_timespec64(etime);
	ts_vblank_time = ktime_to_timespec64(*vblank_time);

	drm_dbg_vbl(dev,
		    "crtc %u : v p(%d,%d)@ %lld.%06ld -> %lld.%06ld [e %d us, %d rep]\n",
		    pipe, hpos, vpos,
		    (u64)ts_etime.tv_sec, ts_etime.tv_nsec / 1000,
		    (u64)ts_vblank_time.tv_sec, ts_vblank_time.tv_nsec / 1000,
		    duration_ns / 1000, i);

	return true;
}
EXPORT_SYMBOL(drm_crtc_vblank_helper_get_vblank_timestamp_internal);

/**
 * drm_crtc_vblank_helper_get_vblank_timestamp - precise vblank timestamp
 *                                               helper
 * @crtc: CRTC whose vblank timestamp to retrieve
 * @max_error: Desired maximum allowable error in timestamps (nanosecs)
 *             On return contains true maximum error of timestamp
 * @vblank_time: Pointer to time which should receive the timestamp
 * @in_vblank_irq:
 *     True when called from drm_crtc_handle_vblank().  Some drivers
 *     need to apply some workarounds for gpu-specific vblank irq quirks
 *     if flag is set.
 *
 * Implements calculation of exact vblank timestamps from given drm_display_mode
 * timings and current video scanout position of a CRTC. This can be directly
 * used as the &drm_crtc_funcs.get_vblank_timestamp implementation of a kms
 * driver if &drm_crtc_helper_funcs.get_scanout_position is implemented.
 *
 * The current implementation only handles standard video modes. For double scan
 * and interlaced modes the driver is supposed to adjust the hardware mode
 * (taken from &drm_crtc_state.adjusted mode for atomic modeset drivers) to
 * match the scanout position reported.
 *
 * Note that atomic drivers must call drm_calc_timestamping_constants() before
 * enabling a CRTC. The atomic helpers already take care of that in
 * drm_atomic_helper_calc_timestamping_constants().
 *
 * Returns:
 * Returns true on success, and false on failure, i.e. when no accurate
 * timestamp could be acquired.
 */
bool drm_crtc_vblank_helper_get_vblank_timestamp(struct drm_crtc *crtc,
						 int *max_error,
						 ktime_t *vblank_time,
						 bool in_vblank_irq)
{
	return drm_crtc_vblank_helper_get_vblank_timestamp_internal(
		crtc, max_error, vblank_time, in_vblank_irq,
		crtc->helper_private->get_scanout_position);
}
EXPORT_SYMBOL(drm_crtc_vblank_helper_get_vblank_timestamp);

/**
 * drm_crtc_get_last_vbltimestamp - retrieve raw timestamp for the most
 *                                  recent vblank interval
 * @crtc: CRTC whose vblank timestamp to retrieve
 * @tvblank: Pointer to target time which should receive the timestamp
 * @in_vblank_irq:
 *     True when called from drm_crtc_handle_vblank().  Some drivers
 *     need to apply some workarounds for gpu-specific vblank irq quirks
 *     if flag is set.
 *
 * Fetches the system timestamp corresponding to the time of the most recent
 * vblank interval on specified CRTC. May call into kms-driver to
 * compute the timestamp with a high-precision GPU specific method.
 *
 * Returns zero if timestamp originates from uncorrected do_gettimeofday()
 * call, i.e., it isn't very precisely locked to the true vblank.
 *
 * Returns:
 * True if timestamp is considered to be very precise, false otherwise.
 */
static bool
drm_crtc_get_last_vbltimestamp(struct drm_crtc *crtc, ktime_t *tvblank,
			       bool in_vblank_irq)
{
	bool ret = false;

	/* Define requested maximum error on timestamps (nanoseconds). */
	int max_error = (int) drm_timestamp_precision * 1000;

	/* Query driver if possible and precision timestamping enabled. */
	if (crtc && crtc->funcs->get_vblank_timestamp && max_error > 0) {
		ret = crtc->funcs->get_vblank_timestamp(crtc, &max_error,
							tvblank, in_vblank_irq);
	}

	/* GPU high precision timestamp query unsupported or failed.
	 * Return current monotonic/gettimeofday timestamp as best estimate.
	 */
	if (!ret)
		*tvblank = ktime_get();

	return ret;
}

static bool
drm_get_last_vbltimestamp(struct drm_device *dev, unsigned int pipe,
			  ktime_t *tvblank, bool in_vblank_irq)
{
	struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

	return drm_crtc_get_last_vbltimestamp(crtc, tvblank, in_vblank_irq);
}

/**
 * drm_crtc_vblank_count - retrieve "cooked" vblank counter value
 * @crtc: which counter to retrieve
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity. Note that this timer isn't correct against a racing
 * vblank interrupt (since it only reports the software vblank counter), see
 * drm_crtc_accurate_vblank_count() for such use-cases.
 *
 * Note that for a given vblank counter value drm_crtc_handle_vblank()
 * and drm_crtc_vblank_count() or drm_crtc_vblank_count_and_time()
 * provide a barrier: Any writes done before calling
 * drm_crtc_handle_vblank() will be visible to callers of the later
 * functions, if the vblank count is the same or a later one.
 *
 * See also &drm_vblank_crtc.count.
 *
 * Returns:
 * The software vblank counter.
 */
u64 drm_crtc_vblank_count(struct drm_crtc *crtc)
{
	return drm_vblank_count(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_count);

/**
 * drm_vblank_count_and_time - retrieve "cooked" vblank counter value and the
 *     system timestamp corresponding to that vblank counter value.
 * @dev: DRM device
 * @pipe: index of CRTC whose counter to retrieve
 * @vblanktime: Pointer to ktime_t to receive the vblank timestamp.
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity. Returns corresponding system timestamp of the time
 * of the vblank interval that corresponds to the current vblank counter value.
 *
 * This is the legacy version of drm_crtc_vblank_count_and_time().
 */
static u64 drm_vblank_count_and_time(struct drm_device *dev, unsigned int pipe,
				     ktime_t *vblanktime)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	u64 vblank_count;
	unsigned int seq;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs)) {
		*vblanktime = 0;
		return 0;
	}

	do {
		seq = read_seqbegin(&vblank->seqlock);
		vblank_count = atomic64_read(&vblank->count);
		*vblanktime = vblank->time;
	} while (read_seqretry(&vblank->seqlock, seq));

	return vblank_count;
}

/**
 * drm_crtc_vblank_count_and_time - retrieve "cooked" vblank counter value
 *     and the system timestamp corresponding to that vblank counter value
 * @crtc: which counter to retrieve
 * @vblanktime: Pointer to time to receive the vblank timestamp.
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity. Returns corresponding system timestamp of the time
 * of the vblank interval that corresponds to the current vblank counter value.
 *
 * Note that for a given vblank counter value drm_crtc_handle_vblank()
 * and drm_crtc_vblank_count() or drm_crtc_vblank_count_and_time()
 * provide a barrier: Any writes done before calling
 * drm_crtc_handle_vblank() will be visible to callers of the later
 * functions, if the vblank count is the same or a later one.
 *
 * See also &drm_vblank_crtc.count.
 */
u64 drm_crtc_vblank_count_and_time(struct drm_crtc *crtc,
				   ktime_t *vblanktime)
{
	return drm_vblank_count_and_time(crtc->dev, drm_crtc_index(crtc),
					 vblanktime);
}
EXPORT_SYMBOL(drm_crtc_vblank_count_and_time);

/**
 * drm_crtc_next_vblank_start - calculate the time of the next vblank
 * @crtc: the crtc for which to calculate next vblank time
 * @vblanktime: pointer to time to receive the next vblank timestamp.
 *
 * Calculate the expected time of the start of the next vblank period,
 * based on time of previous vblank and frame duration
 */
int drm_crtc_next_vblank_start(struct drm_crtc *crtc, ktime_t *vblanktime)
{
	struct drm_vblank_crtc *vblank;
	struct drm_display_mode *mode;
	u64 vblank_start;

	if (!drm_dev_has_vblank(crtc->dev))
		return -EINVAL;

	vblank = drm_crtc_vblank_crtc(crtc);
	mode = &vblank->hwmode;

	if (!vblank->framedur_ns || !vblank->linedur_ns)
		return -EINVAL;

	if (!drm_crtc_get_last_vbltimestamp(crtc, vblanktime, false))
		return -EINVAL;

	vblank_start = DIV_ROUND_DOWN_ULL(
			(u64)vblank->framedur_ns * mode->crtc_vblank_start,
			mode->crtc_vtotal);
	*vblanktime  = ktime_add(*vblanktime, ns_to_ktime(vblank_start));

	return 0;
}
EXPORT_SYMBOL(drm_crtc_next_vblank_start);

static void send_vblank_event(struct drm_device *dev,
		struct drm_pending_vblank_event *e,
		u64 seq, ktime_t now)
{
	struct timespec64 tv;

	switch (e->event.base.type) {
	case DRM_EVENT_VBLANK:
	case DRM_EVENT_FLIP_COMPLETE:
		tv = ktime_to_timespec64(now);
		e->event.vbl.sequence = seq;
		/*
		 * e->event is a user space structure, with hardcoded unsigned
		 * 32-bit seconds/microseconds. This is safe as we always use
		 * monotonic timestamps since linux-4.15
		 */
		e->event.vbl.tv_sec = tv.tv_sec;
		e->event.vbl.tv_usec = tv.tv_nsec / 1000;
		break;
	case DRM_EVENT_CRTC_SEQUENCE:
		if (seq)
			e->event.seq.sequence = seq;
		e->event.seq.time_ns = ktime_to_ns(now);
		break;
	}
	trace_drm_vblank_event_delivered(e->base.file_priv, e->pipe, seq);
	/*
	 * Use the same timestamp for any associated fence signal to avoid
	 * mismatch in timestamps for vsync & fence events triggered by the
	 * same HW event. Frameworks like SurfaceFlinger in Android expects the
	 * retire-fence timestamp to match exactly with HW vsync as it uses it
	 * for its software vsync modeling.
	 */
	drm_send_event_timestamp_locked(dev, &e->base, now);
}

/**
 * drm_crtc_arm_vblank_event - arm vblank event after pageflip
 * @crtc: the source CRTC of the vblank event
 * @e: the event to send
 *
 * A lot of drivers need to generate vblank events for the very next vblank
 * interrupt. For example when the page flip interrupt happens when the page
 * flip gets armed, but not when it actually executes within the next vblank
 * period. This helper function implements exactly the required vblank arming
 * behaviour.
 *
 * NOTE: Drivers using this to send out the &drm_crtc_state.event as part of an
 * atomic commit must ensure that the next vblank happens at exactly the same
 * time as the atomic commit is committed to the hardware. This function itself
 * does **not** protect against the next vblank interrupt racing with either this
 * function call or the atomic commit operation. A possible sequence could be:
 *
 * 1. Driver commits new hardware state into vblank-synchronized registers.
 * 2. A vblank happens, committing the hardware state. Also the corresponding
 *    vblank interrupt is fired off and fully processed by the interrupt
 *    handler.
 * 3. The atomic commit operation proceeds to call drm_crtc_arm_vblank_event().
 * 4. The event is only send out for the next vblank, which is wrong.
 *
 * An equivalent race can happen when the driver calls
 * drm_crtc_arm_vblank_event() before writing out the new hardware state.
 *
 * The only way to make this work safely is to prevent the vblank from firing
 * (and the hardware from committing anything else) until the entire atomic
 * commit sequence has run to completion. If the hardware does not have such a
 * feature (e.g. using a "go" bit), then it is unsafe to use this functions.
 * Instead drivers need to manually send out the event from their interrupt
 * handler by calling drm_crtc_send_vblank_event() and make sure that there's no
 * possible race with the hardware committing the atomic update.
 *
 * Caller must hold a vblank reference for the event @e acquired by a
 * drm_crtc_vblank_get(), which will be dropped when the next vblank arrives.
 */
void drm_crtc_arm_vblank_event(struct drm_crtc *crtc,
			       struct drm_pending_vblank_event *e)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);

	assert_spin_locked(&dev->event_lock);

	e->pipe = pipe;
	e->sequence = drm_crtc_accurate_vblank_count(crtc) + 1;
	list_add_tail(&e->base.link, &dev->vblank_event_list);
}
EXPORT_SYMBOL(drm_crtc_arm_vblank_event);

/**
 * drm_crtc_send_vblank_event - helper to send vblank event after pageflip
 * @crtc: the source CRTC of the vblank event
 * @e: the event to send
 *
 * Updates sequence # and timestamp on event for the most recently processed
 * vblank, and sends it to userspace.  Caller must hold event lock.
 *
 * See drm_crtc_arm_vblank_event() for a helper which can be used in certain
 * situation, especially to send out events for atomic commit operations.
 */
void drm_crtc_send_vblank_event(struct drm_crtc *crtc,
				struct drm_pending_vblank_event *e)
{
	struct drm_device *dev = crtc->dev;
	u64 seq;
	unsigned int pipe = drm_crtc_index(crtc);
	ktime_t now;

	if (drm_dev_has_vblank(dev)) {
		seq = drm_vblank_count_and_time(dev, pipe, &now);
	} else {
		seq = 0;

		now = ktime_get();
	}
	e->pipe = pipe;
	send_vblank_event(dev, e, seq, now);
}
EXPORT_SYMBOL(drm_crtc_send_vblank_event);

static int __enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

		if (drm_WARN_ON(dev, !crtc))
			return 0;

		if (crtc->funcs->enable_vblank)
			return crtc->funcs->enable_vblank(crtc);
	}

	return -EINVAL;
}

static int drm_vblank_enable(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	int ret = 0;

	assert_spin_locked(&dev->vbl_lock);

	spin_lock(&dev->vblank_time_lock);

	if (!vblank->enabled) {
		/*
		 * Enable vblank irqs under vblank_time_lock protection.
		 * All vblank count & timestamp updates are held off
		 * until we are done reinitializing master counter and
		 * timestamps. Filtercode in drm_handle_vblank() will
		 * prevent double-accounting of same vblank interval.
		 */
		ret = __enable_vblank(dev, pipe);
		drm_dbg_core(dev, "enabling vblank on crtc %u, ret: %d\n",
			     pipe, ret);
		if (ret) {
			atomic_dec(&vblank->refcount);
		} else {
			drm_update_vblank_count(dev, pipe, 0);
			/* drm_update_vblank_count() includes a wmb so we just
			 * need to ensure that the compiler emits the write
			 * to mark the vblank as enabled after the call
			 * to drm_update_vblank_count().
			 */
			WRITE_ONCE(vblank->enabled, true);
		}
	}

	spin_unlock(&dev->vblank_time_lock);

	return ret;
}

int drm_vblank_get(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	unsigned long irqflags;
	int ret = 0;

	if (!drm_dev_has_vblank(dev))
		return -EINVAL;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return -EINVAL;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	/* Going from 0->1 means we have to enable interrupts again */
	if (atomic_add_return(1, &vblank->refcount) == 1) {
		ret = drm_vblank_enable(dev, pipe);
	} else {
		if (!vblank->enabled) {
			atomic_dec(&vblank->refcount);
			ret = -EINVAL;
		}
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

	return ret;
}

/**
 * drm_crtc_vblank_get - get a reference count on vblank events
 * @crtc: which CRTC to own
 *
 * Acquire a reference count on vblank events to avoid having them disabled
 * while in use.
 *
 * Returns:
 * Zero on success or a negative error code on failure.
 */
int drm_crtc_vblank_get(struct drm_crtc *crtc)
{
	return drm_vblank_get(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_get);

void drm_vblank_put(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	int vblank_offdelay = vblank->config.offdelay_ms;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return;

	if (drm_WARN_ON(dev, atomic_read(&vblank->refcount) == 0))
		return;

	/* Last user schedules interrupt disable */
	if (atomic_dec_and_test(&vblank->refcount)) {
		if (!vblank_offdelay)
			return;
		else if (vblank_offdelay < 0)
			vblank_disable_fn(vblank);
		else if (!vblank->config.disable_immediate)
			mod_timer(&vblank->disable_timer,
				  jiffies + ((vblank_offdelay * HZ) / 1000));
	}
}

/**
 * drm_crtc_vblank_put - give up ownership of vblank events
 * @crtc: which counter to give up
 *
 * Release ownership of a given vblank counter, turning off interrupts
 * if possible. Disable interrupts after &drm_vblank_crtc_config.offdelay_ms
 * milliseconds.
 */
void drm_crtc_vblank_put(struct drm_crtc *crtc)
{
	drm_vblank_put(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_put);

/**
 * drm_wait_one_vblank - wait for one vblank
 * @dev: DRM device
 * @pipe: CRTC index
 *
 * This waits for one vblank to pass on @pipe, using the irq driver interfaces.
 * It is a failure to call this when the vblank irq for @pipe is disabled, e.g.
 * due to lack of driver support or because the crtc is off.
 *
 * This is the legacy version of drm_crtc_wait_one_vblank().
 */
void drm_wait_one_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	int ret;
	u64 last;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return;

#ifdef __OpenBSD__
	/*
	 * If we're cold, vblank interrupts won't happen even if
	 * they're turned on by the driver.  Just stall long enough
	 * for a vblank to pass.  This assumes a vrefresh of at least
	 * 25 Hz.
	 */
	if (cold) {
		delay(40000);
		return;
	}
#endif

	ret = drm_vblank_get(dev, pipe);
	if (drm_WARN(dev, ret, "vblank not available on crtc %i, ret=%i\n",
		     pipe, ret))
		return;

	last = drm_vblank_count(dev, pipe);

	ret = wait_event_timeout(vblank->queue,
				 last != drm_vblank_count(dev, pipe),
				 msecs_to_jiffies(100));

	drm_WARN(dev, ret == 0, "vblank wait timed out on crtc %i\n", pipe);

	drm_vblank_put(dev, pipe);
}
EXPORT_SYMBOL(drm_wait_one_vblank);

/**
 * drm_crtc_wait_one_vblank - wait for one vblank
 * @crtc: DRM crtc
 *
 * This waits for one vblank to pass on @crtc, using the irq driver interfaces.
 * It is a failure to call this when the vblank irq for @crtc is disabled, e.g.
 * due to lack of driver support or because the crtc is off.
 */
void drm_crtc_wait_one_vblank(struct drm_crtc *crtc)
{
	drm_wait_one_vblank(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_wait_one_vblank);

/**
 * drm_crtc_vblank_off - disable vblank events on a CRTC
 * @crtc: CRTC in question
 *
 * Drivers can use this function to shut down the vblank interrupt handling when
 * disabling a crtc. This function ensures that the latest vblank frame count is
 * stored so that drm_vblank_on can restore it again.
 *
 * Drivers must use this function when the hardware vblank counter can get
 * reset, e.g. when suspending or disabling the @crtc in general.
 */
void drm_crtc_vblank_off(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(crtc);
	struct drm_pending_vblank_event *e, *t;
	ktime_t now;
	u64 seq;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return;

	/*
	 * Grab event_lock early to prevent vblank work from being scheduled
	 * while we're in the middle of shutting down vblank interrupts
	 */
	spin_lock_irq(&dev->event_lock);

	spin_lock(&dev->vbl_lock);
	drm_dbg_vbl(dev, "crtc %d, vblank enabled %d, inmodeset %d\n",
		    pipe, vblank->enabled, vblank->inmodeset);

	/* Avoid redundant vblank disables without previous
	 * drm_crtc_vblank_on(). */
	if (drm_core_check_feature(dev, DRIVER_ATOMIC) || !vblank->inmodeset)
		drm_vblank_disable_and_save(dev, pipe);

	wake_up(&vblank->queue);

	/*
	 * Prevent subsequent drm_vblank_get() from re-enabling
	 * the vblank interrupt by bumping the refcount.
	 */
	if (!vblank->inmodeset) {
		atomic_inc(&vblank->refcount);
		vblank->inmodeset = 1;
	}
	spin_unlock(&dev->vbl_lock);

	/* Send any queued vblank events, lest the natives grow disquiet */
	seq = drm_vblank_count_and_time(dev, pipe, &now);

	list_for_each_entry_safe(e, t, &dev->vblank_event_list, base.link) {
		if (e->pipe != pipe)
			continue;
		drm_dbg_core(dev, "Sending premature vblank event on disable: "
			     "wanted %llu, current %llu\n",
			     e->sequence, seq);
		list_del(&e->base.link);
		drm_vblank_put(dev, pipe);
		send_vblank_event(dev, e, seq, now);
	}

	/* Cancel any leftover pending vblank work */
	drm_vblank_cancel_pending_works(vblank);

	spin_unlock_irq(&dev->event_lock);

	/* Will be reset by the modeset helpers when re-enabling the crtc by
	 * calling drm_calc_timestamping_constants(). */
	vblank->hwmode.crtc_clock = 0;

	/* Wait for any vblank work that's still executing to finish */
	drm_vblank_flush_worker(vblank);
}
EXPORT_SYMBOL(drm_crtc_vblank_off);

/**
 * drm_crtc_vblank_reset - reset vblank state to off on a CRTC
 * @crtc: CRTC in question
 *
 * Drivers can use this function to reset the vblank state to off at load time.
 * Drivers should use this together with the drm_crtc_vblank_off() and
 * drm_crtc_vblank_on() functions. The difference compared to
 * drm_crtc_vblank_off() is that this function doesn't save the vblank counter
 * and hence doesn't need to call any driver hooks.
 *
 * This is useful for recovering driver state e.g. on driver load, or on resume.
 */
void drm_crtc_vblank_reset(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(crtc);

	spin_lock_irq(&dev->vbl_lock);
	/*
	 * Prevent subsequent drm_vblank_get() from enabling the vblank
	 * interrupt by bumping the refcount.
	 */
	if (!vblank->inmodeset) {
		atomic_inc(&vblank->refcount);
		vblank->inmodeset = 1;
	}
	spin_unlock_irq(&dev->vbl_lock);

	drm_WARN_ON(dev, !list_empty(&dev->vblank_event_list));
	drm_WARN_ON(dev, !list_empty(&vblank->pending_work));
}
EXPORT_SYMBOL(drm_crtc_vblank_reset);

/**
 * drm_crtc_set_max_vblank_count - configure the hw max vblank counter value
 * @crtc: CRTC in question
 * @max_vblank_count: max hardware vblank counter value
 *
 * Update the maximum hardware vblank counter value for @crtc
 * at runtime. Useful for hardware where the operation of the
 * hardware vblank counter depends on the currently active
 * display configuration.
 *
 * For example, if the hardware vblank counter does not work
 * when a specific connector is active the maximum can be set
 * to zero. And when that specific connector isn't active the
 * maximum can again be set to the appropriate non-zero value.
 *
 * If used, must be called before drm_vblank_on().
 */
void drm_crtc_set_max_vblank_count(struct drm_crtc *crtc,
				   u32 max_vblank_count)
{
	struct drm_device *dev = crtc->dev;
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(crtc);

	drm_WARN_ON(dev, dev->max_vblank_count);
	drm_WARN_ON(dev, !READ_ONCE(vblank->inmodeset));

	vblank->max_vblank_count = max_vblank_count;
}
EXPORT_SYMBOL(drm_crtc_set_max_vblank_count);

/**
 * drm_crtc_vblank_on_config - enable vblank events on a CRTC with custom
 *     configuration options
 * @crtc: CRTC in question
 * @config: Vblank configuration value
 *
 * See drm_crtc_vblank_on(). In addition, this function allows you to provide a
 * custom vblank configuration for a given CRTC.
 *
 * Note that @config is copied, the pointer does not need to stay valid beyond
 * this function call. For details of the parameters see
 * struct drm_vblank_crtc_config.
 */
void drm_crtc_vblank_on_config(struct drm_crtc *crtc,
			       const struct drm_vblank_crtc_config *config)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(crtc);

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return;

	spin_lock_irq(&dev->vbl_lock);
	drm_dbg_vbl(dev, "crtc %d, vblank enabled %d, inmodeset %d\n",
		    pipe, vblank->enabled, vblank->inmodeset);

	vblank->config = *config;

	/* Drop our private "prevent drm_vblank_get" refcount */
	if (vblank->inmodeset) {
		atomic_dec(&vblank->refcount);
		vblank->inmodeset = 0;
	}

	drm_reset_vblank_timestamp(dev, pipe);

	/*
	 * re-enable interrupts if there are users left, or the
	 * user wishes vblank interrupts to be enabled all the time.
	 */
	if (atomic_read(&vblank->refcount) != 0 || !vblank->config.offdelay_ms)
		drm_WARN_ON(dev, drm_vblank_enable(dev, pipe));
	spin_unlock_irq(&dev->vbl_lock);
}
EXPORT_SYMBOL(drm_crtc_vblank_on_config);

/**
 * drm_crtc_vblank_on - enable vblank events on a CRTC
 * @crtc: CRTC in question
 *
 * This functions restores the vblank interrupt state captured with
 * drm_crtc_vblank_off() again and is generally called when enabling @crtc. Note
 * that calls to drm_crtc_vblank_on() and drm_crtc_vblank_off() can be
 * unbalanced and so can also be unconditionally called in driver load code to
 * reflect the current hardware state of the crtc.
 *
 * Note that unlike in drm_crtc_vblank_on_config(), default values are used.
 */
void drm_crtc_vblank_on(struct drm_crtc *crtc)
{
	const struct drm_vblank_crtc_config config = {
		.offdelay_ms = drm_vblank_offdelay,
		.disable_immediate = crtc->dev->vblank_disable_immediate
	};

	drm_crtc_vblank_on_config(crtc, &config);
}
EXPORT_SYMBOL(drm_crtc_vblank_on);

static void drm_vblank_restore(struct drm_device *dev, unsigned int pipe)
{
	ktime_t t_vblank;
	struct drm_vblank_crtc *vblank;
	int framedur_ns;
	u64 diff_ns;
	u32 cur_vblank, diff = 1;
	int count = DRM_TIMESTAMP_MAXRETRIES;
	u32 max_vblank_count = drm_max_vblank_count(dev, pipe);

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return;

	assert_spin_locked(&dev->vbl_lock);
	assert_spin_locked(&dev->vblank_time_lock);

	vblank = drm_vblank_crtc(dev, pipe);
	drm_WARN_ONCE(dev,
		      drm_debug_enabled(DRM_UT_VBL) && !vblank->framedur_ns,
		      "Cannot compute missed vblanks without frame duration\n");
	framedur_ns = vblank->framedur_ns;

	do {
		cur_vblank = __get_vblank_counter(dev, pipe);
		drm_get_last_vbltimestamp(dev, pipe, &t_vblank, false);
	} while (cur_vblank != __get_vblank_counter(dev, pipe) && --count > 0);

	diff_ns = ktime_to_ns(ktime_sub(t_vblank, vblank->time));
	if (framedur_ns)
		diff = DIV_ROUND_CLOSEST_ULL(diff_ns, framedur_ns);


	drm_dbg_vbl(dev,
		    "missed %d vblanks in %lld ns, frame duration=%d ns, hw_diff=%d\n",
		    diff, diff_ns, framedur_ns, cur_vblank - vblank->last);
	vblank->last = (cur_vblank - diff) & max_vblank_count;
}

/**
 * drm_crtc_vblank_restore - estimate missed vblanks and update vblank count.
 * @crtc: CRTC in question
 *
 * Power manamement features can cause frame counter resets between vblank
 * disable and enable. Drivers can use this function in their
 * &drm_crtc_funcs.enable_vblank implementation to estimate missed vblanks since
 * the last &drm_crtc_funcs.disable_vblank using timestamps and update the
 * vblank counter.
 *
 * Note that drivers must have race-free high-precision timestamping support,
 * i.e.  &drm_crtc_funcs.get_vblank_timestamp must be hooked up and
 * &drm_vblank_crtc_config.disable_immediate must be set to indicate the
 * time-stamping functions are race-free against vblank hardware counter
 * increments.
 */
void drm_crtc_vblank_restore(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);

	drm_WARN_ON_ONCE(dev, !crtc->funcs->get_vblank_timestamp);
	drm_WARN_ON_ONCE(dev, vblank->inmodeset);
	drm_WARN_ON_ONCE(dev, !vblank->config.disable_immediate);

	drm_vblank_restore(dev, pipe);
}
EXPORT_SYMBOL(drm_crtc_vblank_restore);

static int drm_queue_vblank_event(struct drm_device *dev, unsigned int pipe,
				  u64 req_seq,
				  union drm_wait_vblank *vblwait,
				  struct drm_file *file_priv)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	struct drm_pending_vblank_event *e;
	ktime_t now;
	u64 seq;
	int ret;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL) {
		ret = -ENOMEM;
		goto err_put;
	}

	e->pipe = pipe;
	e->event.base.type = DRM_EVENT_VBLANK;
	e->event.base.length = sizeof(e->event.vbl);
	e->event.vbl.user_data = vblwait->request.signal;
	e->event.vbl.crtc_id = 0;
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

		if (crtc)
			e->event.vbl.crtc_id = crtc->base.id;
	}

	spin_lock_irq(&dev->event_lock);

	/*
	 * drm_crtc_vblank_off() might have been called after we called
	 * drm_vblank_get(). drm_crtc_vblank_off() holds event_lock around the
	 * vblank disable, so no need for further locking.  The reference from
	 * drm_vblank_get() protects against vblank disable from another source.
	 */
	if (!READ_ONCE(vblank->enabled)) {
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = drm_event_reserve_init_locked(dev, file_priv, &e->base,
					    &e->event.base);

	if (ret)
		goto err_unlock;

	seq = drm_vblank_count_and_time(dev, pipe, &now);

	drm_dbg_core(dev, "event on vblank count %llu, current %llu, crtc %u\n",
		     req_seq, seq, pipe);

	trace_drm_vblank_event_queued(file_priv, pipe, req_seq);

	e->sequence = req_seq;
	if (drm_vblank_passed(seq, req_seq)) {
		drm_vblank_put(dev, pipe);
		send_vblank_event(dev, e, seq, now);
		vblwait->reply.sequence = seq;
	} else {
		/* drm_handle_vblank_events will call drm_vblank_put */
		list_add_tail(&e->base.link, &dev->vblank_event_list);
		vblwait->reply.sequence = req_seq;
	}

	spin_unlock_irq(&dev->event_lock);

	return 0;

err_unlock:
	spin_unlock_irq(&dev->event_lock);
	kfree(e);
err_put:
	drm_vblank_put(dev, pipe);
	return ret;
}

static bool drm_wait_vblank_is_query(union drm_wait_vblank *vblwait)
{
	if (vblwait->request.sequence)
		return false;

	return _DRM_VBLANK_RELATIVE ==
		(vblwait->request.type & (_DRM_VBLANK_TYPES_MASK |
					  _DRM_VBLANK_EVENT |
					  _DRM_VBLANK_NEXTONMISS));
}

/*
 * Widen a 32-bit param to 64-bits.
 *
 * \param narrow 32-bit value (missing upper 32 bits)
 * \param near 64-bit value that should be 'close' to near
 *
 * This function returns a 64-bit value using the lower 32-bits from
 * 'narrow' and constructing the upper 32-bits so that the result is
 * as close as possible to 'near'.
 */

static u64 widen_32_to_64(u32 narrow, u64 near)
{
	return near + (s32) (narrow - near);
}

static void drm_wait_vblank_reply(struct drm_device *dev, unsigned int pipe,
				  struct drm_wait_vblank_reply *reply)
{
	ktime_t now;
	struct timespec64 ts;

	/*
	 * drm_wait_vblank_reply is a UAPI structure that uses 'long'
	 * to store the seconds. This is safe as we always use monotonic
	 * timestamps since linux-4.15.
	 */
	reply->sequence = drm_vblank_count_and_time(dev, pipe, &now);
	ts = ktime_to_timespec64(now);
	reply->tval_sec = (u32)ts.tv_sec;
	reply->tval_usec = ts.tv_nsec / 1000;
}

static bool drm_wait_vblank_supported(struct drm_device *dev)
{
	return drm_dev_has_vblank(dev);
}

int drm_wait_vblank_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	struct drm_vblank_crtc *vblank;
	union drm_wait_vblank *vblwait = data;
	int ret;
	u64 req_seq, seq;
	unsigned int pipe_index;
	unsigned int flags, pipe, high_pipe;

	if (!drm_wait_vblank_supported(dev))
		return -EOPNOTSUPP;

	if (vblwait->request.type & _DRM_VBLANK_SIGNAL)
		return -EINVAL;

	if (vblwait->request.type &
	    ~(_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK |
	      _DRM_VBLANK_HIGH_CRTC_MASK)) {
		drm_dbg_core(dev,
			     "Unsupported type value 0x%x, supported mask 0x%x\n",
			     vblwait->request.type,
			     (_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK |
			      _DRM_VBLANK_HIGH_CRTC_MASK));
		return -EINVAL;
	}

	flags = vblwait->request.type & _DRM_VBLANK_FLAGS_MASK;
	high_pipe = (vblwait->request.type & _DRM_VBLANK_HIGH_CRTC_MASK);
	if (high_pipe)
		pipe_index = high_pipe >> _DRM_VBLANK_HIGH_CRTC_SHIFT;
	else
		pipe_index = flags & _DRM_VBLANK_SECONDARY ? 1 : 0;

	/* Convert lease-relative crtc index into global crtc index */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		pipe = 0;
		drm_for_each_crtc(crtc, dev) {
			if (drm_lease_held(file_priv, crtc->base.id)) {
				if (pipe_index == 0)
					break;
				pipe_index--;
			}
			pipe++;
		}
	} else {
		pipe = pipe_index;
	}

	if (pipe >= dev->num_crtcs)
		return -EINVAL;

	vblank = &dev->vblank[pipe];

	/* If the counter is currently enabled and accurate, short-circuit
	 * queries to return the cached timestamp of the last vblank.
	 */
	if (vblank->config.disable_immediate &&
	    drm_wait_vblank_is_query(vblwait) &&
	    READ_ONCE(vblank->enabled)) {
		drm_wait_vblank_reply(dev, pipe, &vblwait->reply);
		return 0;
	}

	ret = drm_vblank_get(dev, pipe);
	if (ret) {
		drm_dbg_core(dev,
			     "crtc %d failed to acquire vblank counter, %d\n",
			     pipe, ret);
		return ret;
	}
	seq = drm_vblank_count(dev, pipe);

	switch (vblwait->request.type & _DRM_VBLANK_TYPES_MASK) {
	case _DRM_VBLANK_RELATIVE:
		req_seq = seq + vblwait->request.sequence;
		vblwait->request.sequence = req_seq;
		vblwait->request.type &= ~_DRM_VBLANK_RELATIVE;
		break;
	case _DRM_VBLANK_ABSOLUTE:
		req_seq = widen_32_to_64(vblwait->request.sequence, seq);
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	if ((flags & _DRM_VBLANK_NEXTONMISS) &&
	    drm_vblank_passed(seq, req_seq)) {
		req_seq = seq + 1;
		vblwait->request.type &= ~_DRM_VBLANK_NEXTONMISS;
		vblwait->request.sequence = req_seq;
	}

	if (flags & _DRM_VBLANK_EVENT) {
		/* must hold on to the vblank ref until the event fires
		 * drm_vblank_put will be called asynchronously
		 */
		return drm_queue_vblank_event(dev, pipe, req_seq, vblwait, file_priv);
	}

	if (req_seq != seq) {
		int wait;

		drm_dbg_core(dev, "waiting on vblank count %llu, crtc %u\n",
			     req_seq, pipe);
		wait = wait_event_interruptible_timeout(vblank->queue,
			drm_vblank_passed(drm_vblank_count(dev, pipe), req_seq) ||
				      !READ_ONCE(vblank->enabled),
			msecs_to_jiffies(3000));

		switch (wait) {
		case 0:
			/* timeout */
			ret = -EBUSY;
			break;
		case -ERESTARTSYS:
			/* interrupted by signal */
			ret = -EINTR;
			break;
		default:
			ret = 0;
			break;
		}
	}

	if (ret != -EINTR) {
		drm_wait_vblank_reply(dev, pipe, &vblwait->reply);

		drm_dbg_core(dev, "crtc %d returning %u to client\n",
			     pipe, vblwait->reply.sequence);
	} else {
		drm_dbg_core(dev, "crtc %d vblank wait interrupted by signal\n",
			     pipe);
	}

done:
	drm_vblank_put(dev, pipe);
	return ret;
}

static void drm_handle_vblank_events(struct drm_device *dev, unsigned int pipe)
{
	struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);
	bool high_prec = false;
	struct drm_pending_vblank_event *e, *t;
	ktime_t now;
	u64 seq;

	assert_spin_locked(&dev->event_lock);

	seq = drm_vblank_count_and_time(dev, pipe, &now);

	list_for_each_entry_safe(e, t, &dev->vblank_event_list, base.link) {
		if (e->pipe != pipe)
			continue;
		if (!drm_vblank_passed(seq, e->sequence))
			continue;

		drm_dbg_core(dev, "vblank event on %llu, current %llu\n",
			     e->sequence, seq);

		list_del(&e->base.link);
		drm_vblank_put(dev, pipe);
		send_vblank_event(dev, e, seq, now);
	}

	if (crtc && crtc->funcs->get_vblank_timestamp)
		high_prec = true;

	trace_drm_vblank_event(pipe, seq, now, high_prec);
}

/**
 * drm_handle_vblank - handle a vblank event
 * @dev: DRM device
 * @pipe: index of CRTC where this event occurred
 *
 * Drivers should call this routine in their vblank interrupt handlers to
 * update the vblank counter and send any signals that may be pending.
 *
 * This is the legacy version of drm_crtc_handle_vblank().
 */
bool drm_handle_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = drm_vblank_crtc(dev, pipe);
	unsigned long irqflags;
	bool disable_irq;

	if (drm_WARN_ON_ONCE(dev, !drm_dev_has_vblank(dev)))
		return false;

	if (drm_WARN_ON(dev, pipe >= dev->num_crtcs))
		return false;

	spin_lock_irqsave(&dev->event_lock, irqflags);

	/* Need timestamp lock to prevent concurrent execution with
	 * vblank enable/disable, as this would cause inconsistent
	 * or corrupted timestamps and vblank counts.
	 */
	spin_lock(&dev->vblank_time_lock);

	/* Vblank irq handling disabled. Nothing to do. */
	if (!vblank->enabled) {
		spin_unlock(&dev->vblank_time_lock);
		spin_unlock_irqrestore(&dev->event_lock, irqflags);
		return false;
	}

	drm_update_vblank_count(dev, pipe, true);

	spin_unlock(&dev->vblank_time_lock);

	wake_up(&vblank->queue);

	/* With instant-off, we defer disabling the interrupt until after
	 * we finish processing the following vblank after all events have
	 * been signaled. The disable has to be last (after
	 * drm_handle_vblank_events) so that the timestamp is always accurate.
	 */
	disable_irq = (vblank->config.disable_immediate &&
		       vblank->config.offdelay_ms > 0 &&
		       !atomic_read(&vblank->refcount));

	drm_handle_vblank_events(dev, pipe);
	drm_handle_vblank_works(vblank);

	spin_unlock_irqrestore(&dev->event_lock, irqflags);

	if (disable_irq)
		vblank_disable_fn(vblank);

	return true;
}
EXPORT_SYMBOL(drm_handle_vblank);

/**
 * drm_crtc_handle_vblank - handle a vblank event
 * @crtc: where this event occurred
 *
 * Drivers should call this routine in their vblank interrupt handlers to
 * update the vblank counter and send any signals that may be pending.
 *
 * This is the native KMS version of drm_handle_vblank().
 *
 * Note that for a given vblank counter value drm_crtc_handle_vblank()
 * and drm_crtc_vblank_count() or drm_crtc_vblank_count_and_time()
 * provide a barrier: Any writes done before calling
 * drm_crtc_handle_vblank() will be visible to callers of the later
 * functions, if the vblank count is the same or a later one.
 *
 * See also &drm_vblank_crtc.count.
 *
 * Returns:
 * True if the event was successfully handled, false on failure.
 */
bool drm_crtc_handle_vblank(struct drm_crtc *crtc)
{
	return drm_handle_vblank(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_handle_vblank);

/*
 * Get crtc VBLANK count.
 *
 * \param dev DRM device
 * \param data user argument, pointing to a drm_crtc_get_sequence structure.
 * \param file_priv drm file private for the user's open file descriptor
 */

int drm_crtc_get_sequence_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	struct drm_vblank_crtc *vblank;
	int pipe;
	struct drm_crtc_get_sequence *get_seq = data;
	ktime_t now;
	bool vblank_enabled;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	if (!drm_dev_has_vblank(dev))
		return -EOPNOTSUPP;

	crtc = drm_crtc_find(dev, file_priv, get_seq->crtc_id);
	if (!crtc)
		return -ENOENT;

	pipe = drm_crtc_index(crtc);

	vblank = drm_crtc_vblank_crtc(crtc);
	vblank_enabled = READ_ONCE(vblank->config.disable_immediate) &&
		READ_ONCE(vblank->enabled);

	if (!vblank_enabled) {
		ret = drm_crtc_vblank_get(crtc);
		if (ret) {
			drm_dbg_core(dev,
				     "crtc %d failed to acquire vblank counter, %d\n",
				     pipe, ret);
			return ret;
		}
	}
	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state)
		get_seq->active = crtc->state->enable;
	else
		get_seq->active = crtc->enabled;
	drm_modeset_unlock(&crtc->mutex);
	get_seq->sequence = drm_vblank_count_and_time(dev, pipe, &now);
	get_seq->sequence_ns = ktime_to_ns(now);
	if (!vblank_enabled)
		drm_crtc_vblank_put(crtc);
	return 0;
}

/*
 * Queue a event for VBLANK sequence
 *
 * \param dev DRM device
 * \param data user argument, pointing to a drm_crtc_queue_sequence structure.
 * \param file_priv drm file private for the user's open file descriptor
 */

int drm_crtc_queue_sequence_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	struct drm_vblank_crtc *vblank;
	int pipe;
	struct drm_crtc_queue_sequence *queue_seq = data;
	ktime_t now;
	struct drm_pending_vblank_event *e;
	u32 flags;
	u64 seq;
	u64 req_seq;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	if (!drm_dev_has_vblank(dev))
		return -EOPNOTSUPP;

	crtc = drm_crtc_find(dev, file_priv, queue_seq->crtc_id);
	if (!crtc)
		return -ENOENT;

	flags = queue_seq->flags;
	/* Check valid flag bits */
	if (flags & ~(DRM_CRTC_SEQUENCE_RELATIVE|
		      DRM_CRTC_SEQUENCE_NEXT_ON_MISS))
		return -EINVAL;

	pipe = drm_crtc_index(crtc);

	vblank = drm_crtc_vblank_crtc(crtc);

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	ret = drm_crtc_vblank_get(crtc);
	if (ret) {
		drm_dbg_core(dev,
			     "crtc %d failed to acquire vblank counter, %d\n",
			     pipe, ret);
		goto err_free;
	}

	seq = drm_vblank_count_and_time(dev, pipe, &now);
	req_seq = queue_seq->sequence;

	if (flags & DRM_CRTC_SEQUENCE_RELATIVE)
		req_seq += seq;

	if ((flags & DRM_CRTC_SEQUENCE_NEXT_ON_MISS) && drm_vblank_passed(seq, req_seq))
		req_seq = seq + 1;

	e->pipe = pipe;
	e->event.base.type = DRM_EVENT_CRTC_SEQUENCE;
	e->event.base.length = sizeof(e->event.seq);
	e->event.seq.user_data = queue_seq->user_data;

	spin_lock_irq(&dev->event_lock);

	/*
	 * drm_crtc_vblank_off() might have been called after we called
	 * drm_crtc_vblank_get(). drm_crtc_vblank_off() holds event_lock around the
	 * vblank disable, so no need for further locking.  The reference from
	 * drm_crtc_vblank_get() protects against vblank disable from another source.
	 */
	if (!READ_ONCE(vblank->enabled)) {
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = drm_event_reserve_init_locked(dev, file_priv, &e->base,
					    &e->event.base);

	if (ret)
		goto err_unlock;

	e->sequence = req_seq;

	if (drm_vblank_passed(seq, req_seq)) {
		drm_crtc_vblank_put(crtc);
		send_vblank_event(dev, e, seq, now);
		queue_seq->sequence = seq;
	} else {
		/* drm_handle_vblank_events will call drm_vblank_put */
		list_add_tail(&e->base.link, &dev->vblank_event_list);
		queue_seq->sequence = req_seq;
	}

	spin_unlock_irq(&dev->event_lock);
	return 0;

err_unlock:
	spin_unlock_irq(&dev->event_lock);
	drm_crtc_vblank_put(crtc);
err_free:
	kfree(e);
	return ret;
}

