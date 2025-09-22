/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_RUNTIME_PM_H__
#define __INTEL_RUNTIME_PM_H__

#include <linux/pm_runtime.h>
#include <linux/types.h>

#include "intel_wakeref.h"

struct device;
struct drm_i915_private;
struct drm_printer;

/*
 * This struct helps tracking the state needed for runtime PM, which puts the
 * device in PCI D3 state. Notice that when this happens, nothing on the
 * graphics device works, even register access, so we don't get interrupts nor
 * anything else.
 *
 * Every piece of our code that needs to actually touch the hardware needs to
 * either call intel_runtime_pm_get or call intel_display_power_get with the
 * appropriate power domain.
 *
 * Our driver uses the autosuspend delay feature, which means we'll only really
 * suspend if we stay with zero refcount for a certain amount of time. The
 * default value is currently very conservative (see intel_runtime_pm_enable), but
 * it can be changed with the standard runtime PM files from sysfs.
 *
 * The irqs_disabled variable becomes true exactly after we disable the IRQs and
 * goes back to false exactly before we reenable the IRQs. We use this variable
 * to check if someone is trying to enable/disable IRQs while they're supposed
 * to be disabled. This shouldn't happen and we'll print some error messages in
 * case it happens.
 *
 * For more, read the Documentation/power/runtime_pm.rst.
 */
struct intel_runtime_pm {
	atomic_t wakeref_count;
	struct device *kdev; /* points to i915->drm.dev */
	bool available;
	bool irqs_enabled;
	bool no_wakeref_tracking;

	/*
	 *  Protects access to lmem usefault list.
	 *  It is required, if we are outside of the runtime suspend path,
	 *  access to @lmem_userfault_list requires always first grabbing the
	 *  runtime pm, to ensure we can't race against runtime suspend.
	 *  Once we have that we also need to grab @lmem_userfault_lock,
	 *  at which point we have exclusive access.
	 *  The runtime suspend path is special since it doesn't really hold any locks,
	 *  but instead has exclusive access by virtue of all other accesses requiring
	 *  holding the runtime pm wakeref.
	 */
	spinlock_t lmem_userfault_lock;

	/*
	 *  Keep list of userfaulted gem obj, which require to release their
	 *  mmap mappings at runtime suspend path.
	 */
	struct list_head lmem_userfault_list;

	/* Manual runtime pm autosuspend delay for user GGTT/lmem mmaps */
	struct intel_wakeref_auto userfault_wakeref;

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
	/*
	 * To aide detection of wakeref leaks and general misuse, we
	 * track all wakeref holders. With manual markup (i.e. returning
	 * a cookie to each rpm_get caller which they then supply to their
	 * paired rpm_put) we can remove corresponding pairs of and keep
	 * the array trimmed to active wakerefs.
	 */
	struct ref_tracker_dir debug;
#endif
};

#define BITS_PER_WAKEREF	\
	BITS_PER_TYPE(typeof_member(struct intel_runtime_pm, wakeref_count))
#define INTEL_RPM_WAKELOCK_SHIFT	(BITS_PER_WAKEREF / 2)
#define INTEL_RPM_WAKELOCK_BIAS		(1 << INTEL_RPM_WAKELOCK_SHIFT)
#define INTEL_RPM_RAW_WAKEREF_MASK	(INTEL_RPM_WAKELOCK_BIAS - 1)

static inline int
intel_rpm_raw_wakeref_count(int wakeref_count)
{
	return wakeref_count & INTEL_RPM_RAW_WAKEREF_MASK;
}

static inline int
intel_rpm_wakelock_count(int wakeref_count)
{
	return wakeref_count >> INTEL_RPM_WAKELOCK_SHIFT;
}

static inline void
assert_rpm_device_not_suspended(struct intel_runtime_pm *rpm)
{
	WARN_ONCE(pm_runtime_suspended(rpm->kdev),
		  "Device suspended during HW access\n");
}

static inline void
__assert_rpm_raw_wakeref_held(struct intel_runtime_pm *rpm, int wakeref_count)
{
	assert_rpm_device_not_suspended(rpm);
	WARN_ONCE(!intel_rpm_raw_wakeref_count(wakeref_count),
		  "RPM raw-wakeref not held\n");
}

static inline void
__assert_rpm_wakelock_held(struct intel_runtime_pm *rpm, int wakeref_count)
{
	__assert_rpm_raw_wakeref_held(rpm, wakeref_count);
	WARN_ONCE(!intel_rpm_wakelock_count(wakeref_count),
		  "RPM wakelock ref not held during HW access\n");
}

static inline void
assert_rpm_raw_wakeref_held(struct intel_runtime_pm *rpm)
{
	__assert_rpm_raw_wakeref_held(rpm, atomic_read(&rpm->wakeref_count));
}

static inline void
assert_rpm_wakelock_held(struct intel_runtime_pm *rpm)
{
	__assert_rpm_wakelock_held(rpm, atomic_read(&rpm->wakeref_count));
}

/**
 * disable_rpm_wakeref_asserts - disable the RPM assert checks
 * @rpm: the intel_runtime_pm structure
 *
 * This function disable asserts that check if we hold an RPM wakelock
 * reference, while keeping the device-not-suspended checks still enabled.
 * It's meant to be used only in special circumstances where our rule about
 * the wakelock refcount wrt. the device power state doesn't hold. According
 * to this rule at any point where we access the HW or want to keep the HW in
 * an active state we must hold an RPM wakelock reference acquired via one of
 * the intel_runtime_pm_get() helpers. Currently there are a few special spots
 * where this rule doesn't hold: the IRQ and suspend/resume handlers, the
 * forcewake release timer, and the GPU RPS and hangcheck works. All other
 * users should avoid using this function.
 *
 * Any calls to this function must have a symmetric call to
 * enable_rpm_wakeref_asserts().
 */
static inline void
disable_rpm_wakeref_asserts(struct intel_runtime_pm *rpm)
{
	atomic_add(INTEL_RPM_WAKELOCK_BIAS + 1,
		   &rpm->wakeref_count);
}

/**
 * enable_rpm_wakeref_asserts - re-enable the RPM assert checks
 * @rpm: the intel_runtime_pm structure
 *
 * This function re-enables the RPM assert checks after disabling them with
 * disable_rpm_wakeref_asserts. It's meant to be used only in special
 * circumstances otherwise its use should be avoided.
 *
 * Any calls to this function must have a symmetric call to
 * disable_rpm_wakeref_asserts().
 */
static inline void
enable_rpm_wakeref_asserts(struct intel_runtime_pm *rpm)
{
	atomic_sub(INTEL_RPM_WAKELOCK_BIAS + 1,
		   &rpm->wakeref_count);
}

void intel_runtime_pm_init_early(struct intel_runtime_pm *rpm);
void intel_runtime_pm_enable(struct intel_runtime_pm *rpm);
void intel_runtime_pm_disable(struct intel_runtime_pm *rpm);
void intel_runtime_pm_driver_release(struct intel_runtime_pm *rpm);
void intel_runtime_pm_driver_last_release(struct intel_runtime_pm *rpm);

intel_wakeref_t intel_runtime_pm_get(struct intel_runtime_pm *rpm);
intel_wakeref_t intel_runtime_pm_get_if_in_use(struct intel_runtime_pm *rpm);
intel_wakeref_t intel_runtime_pm_get_if_active(struct intel_runtime_pm *rpm);
intel_wakeref_t intel_runtime_pm_get_noresume(struct intel_runtime_pm *rpm);
intel_wakeref_t intel_runtime_pm_get_raw(struct intel_runtime_pm *rpm);

#define with_intel_runtime_pm(rpm, wf) \
	for ((wf) = intel_runtime_pm_get(rpm); (wf); \
	     intel_runtime_pm_put((rpm), (wf)), (wf) = 0)

#define with_intel_runtime_pm_if_in_use(rpm, wf) \
	for ((wf) = intel_runtime_pm_get_if_in_use(rpm); (wf); \
	     intel_runtime_pm_put((rpm), (wf)), (wf) = 0)

#define with_intel_runtime_pm_if_active(rpm, wf) \
	for ((wf) = intel_runtime_pm_get_if_active(rpm); (wf); \
	     intel_runtime_pm_put((rpm), (wf)), (wf) = 0)

void intel_runtime_pm_put_unchecked(struct intel_runtime_pm *rpm);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_runtime_pm_put(struct intel_runtime_pm *rpm, intel_wakeref_t wref);
#else
static inline void
intel_runtime_pm_put(struct intel_runtime_pm *rpm, intel_wakeref_t wref)
{
	intel_runtime_pm_put_unchecked(rpm);
}
#endif
void intel_runtime_pm_put_raw(struct intel_runtime_pm *rpm, intel_wakeref_t wref);

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void print_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm,
				    struct drm_printer *p);
#else
static inline void print_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm,
						  struct drm_printer *p)
{
}
#endif

#endif /* __INTEL_RUNTIME_PM_H__ */
