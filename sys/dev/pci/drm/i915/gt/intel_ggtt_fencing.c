// SPDX-License-Identifier: MIT
/*
 * Copyright © 2008-2015 Intel Corporation
 */

#include <linux/highmem.h>

#include "display/intel_display.h"
#include "i915_drv.h"
#include "i915_reg.h"
#include "i915_scatterlist.h"
#include "i915_pvinfo.h"
#include "i915_vgpu.h"
#include "intel_gt_regs.h"
#include "intel_mchbar_regs.h"

/**
 * DOC: fence register handling
 *
 * Important to avoid confusions: "fences" in the i915 driver are not execution
 * fences used to track command completion but hardware detiler objects which
 * wrap a given range of the global GTT. Each platform has only a fairly limited
 * set of these objects.
 *
 * Fences are used to detile GTT memory mappings. They're also connected to the
 * hardware frontbuffer render tracking and hence interact with frontbuffer
 * compression. Furthermore on older platforms fences are required for tiled
 * objects used by the display engine. They can also be used by the render
 * engine - they're required for blitter commands and are optional for render
 * commands. But on gen4+ both display (with the exception of fbc) and rendering
 * have their own tiling state bits and don't need fences.
 *
 * Also note that fences only support X and Y tiling and hence can't be used for
 * the fancier new tiling formats like W, Ys and Yf.
 *
 * Finally note that because fences are such a restricted resource they're
 * dynamically associated with objects. Furthermore fence state is committed to
 * the hardware lazily to avoid unnecessary stalls on gen2/3. Therefore code must
 * explicitly call i915_gem_object_get_fence() to synchronize fencing status
 * for cpu access. Also note that some code wants an unfenced view, for those
 * cases the fence can be removed forcefully with i915_gem_object_put_fence().
 *
 * Internally these functions will synchronize with userspace access by removing
 * CPU ptes into GTT mmaps (not the GTT ptes themselves) as needed.
 */

#define pipelined 0

static struct drm_i915_private *fence_to_i915(struct i915_fence_reg *fence)
{
	return fence->ggtt->vm.i915;
}

static struct intel_uncore *fence_to_uncore(struct i915_fence_reg *fence)
{
	return fence->ggtt->vm.gt->uncore;
}

static void i965_write_fence_reg(struct i915_fence_reg *fence)
{
	i915_reg_t fence_reg_lo, fence_reg_hi;
	int fence_pitch_shift;
	u64 val;

	if (GRAPHICS_VER(fence_to_i915(fence)) >= 6) {
		fence_reg_lo = FENCE_REG_GEN6_LO(fence->id);
		fence_reg_hi = FENCE_REG_GEN6_HI(fence->id);
		fence_pitch_shift = GEN6_FENCE_PITCH_SHIFT;

	} else {
		fence_reg_lo = FENCE_REG_965_LO(fence->id);
		fence_reg_hi = FENCE_REG_965_HI(fence->id);
		fence_pitch_shift = I965_FENCE_PITCH_SHIFT;
	}

	val = 0;
	if (fence->tiling) {
		unsigned int stride = fence->stride;

		GEM_BUG_ON(!IS_ALIGNED(stride, 128));

		val = fence->start + fence->size - I965_FENCE_PAGE;
		val <<= 32;
		val |= fence->start;
		val |= (u64)((stride / 128) - 1) << fence_pitch_shift;
		if (fence->tiling == I915_TILING_Y)
			val |= BIT(I965_FENCE_TILING_Y_SHIFT);
		val |= I965_FENCE_REG_VALID;
	}

	if (!pipelined) {
		struct intel_uncore *uncore = fence_to_uncore(fence);

		/*
		 * To w/a incoherency with non-atomic 64-bit register updates,
		 * we split the 64-bit update into two 32-bit writes. In order
		 * for a partial fence not to be evaluated between writes, we
		 * precede the update with write to turn off the fence register,
		 * and only enable the fence as the last step.
		 *
		 * For extra levels of paranoia, we make sure each step lands
		 * before applying the next step.
		 */
		intel_uncore_write_fw(uncore, fence_reg_lo, 0);
		intel_uncore_posting_read_fw(uncore, fence_reg_lo);

		intel_uncore_write_fw(uncore, fence_reg_hi, upper_32_bits(val));
		intel_uncore_write_fw(uncore, fence_reg_lo, lower_32_bits(val));
		intel_uncore_posting_read_fw(uncore, fence_reg_lo);
	}
}

static void i915_write_fence_reg(struct i915_fence_reg *fence)
{
	u32 val;

	val = 0;
	if (fence->tiling) {
		unsigned int stride = fence->stride;
		unsigned int tiling = fence->tiling;
		bool is_y_tiled = tiling == I915_TILING_Y;

		if (is_y_tiled && HAS_128_BYTE_Y_TILING(fence_to_i915(fence)))
			stride /= 128;
		else
			stride /= 512;
		GEM_BUG_ON(!is_power_of_2(stride));

		val = fence->start;
		if (is_y_tiled)
			val |= BIT(I830_FENCE_TILING_Y_SHIFT);
		val |= I915_FENCE_SIZE_BITS(fence->size);
		val |= ilog2(stride) << I830_FENCE_PITCH_SHIFT;

		val |= I830_FENCE_REG_VALID;
	}

	if (!pipelined) {
		struct intel_uncore *uncore = fence_to_uncore(fence);
		i915_reg_t reg = FENCE_REG(fence->id);

		intel_uncore_write_fw(uncore, reg, val);
		intel_uncore_posting_read_fw(uncore, reg);
	}
}

static void i830_write_fence_reg(struct i915_fence_reg *fence)
{
	u32 val;

	val = 0;
	if (fence->tiling) {
		unsigned int stride = fence->stride;

		val = fence->start;
		if (fence->tiling == I915_TILING_Y)
			val |= BIT(I830_FENCE_TILING_Y_SHIFT);
		val |= I830_FENCE_SIZE_BITS(fence->size);
		val |= ilog2(stride / 128) << I830_FENCE_PITCH_SHIFT;
		val |= I830_FENCE_REG_VALID;
	}

	if (!pipelined) {
		struct intel_uncore *uncore = fence_to_uncore(fence);
		i915_reg_t reg = FENCE_REG(fence->id);

		intel_uncore_write_fw(uncore, reg, val);
		intel_uncore_posting_read_fw(uncore, reg);
	}
}

static void fence_write(struct i915_fence_reg *fence)
{
	struct drm_i915_private *i915 = fence_to_i915(fence);

	/*
	 * Previous access through the fence register is marshalled by
	 * the mb() inside the fault handlers (i915_gem_release_mmaps)
	 * and explicitly managed for internal users.
	 */

	if (GRAPHICS_VER(i915) == 2)
		i830_write_fence_reg(fence);
	else if (GRAPHICS_VER(i915) == 3)
		i915_write_fence_reg(fence);
	else
		i965_write_fence_reg(fence);

	/*
	 * Access through the fenced region afterwards is
	 * ordered by the posting reads whilst writing the registers.
	 */
}

static bool gpu_uses_fence_registers(struct i915_fence_reg *fence)
{
	return GRAPHICS_VER(fence_to_i915(fence)) < 4;
}

static int fence_update(struct i915_fence_reg *fence,
			struct i915_vma *vma)
{
	struct i915_ggtt *ggtt = fence->ggtt;
	struct intel_uncore *uncore = fence_to_uncore(fence);
	intel_wakeref_t wakeref;
	struct i915_vma *old;
	int ret;

	fence->tiling = 0;
	if (vma) {
		GEM_BUG_ON(!i915_gem_object_get_stride(vma->obj) ||
			   !i915_gem_object_get_tiling(vma->obj));

		if (!i915_vma_is_map_and_fenceable(vma))
			return -EINVAL;

		if (gpu_uses_fence_registers(fence)) {
			/* implicit 'unfenced' GPU blits */
			ret = i915_vma_sync(vma);
			if (ret)
				return ret;
		}

		GEM_BUG_ON(vma->fence_size > i915_vma_size(vma));
		fence->start = i915_ggtt_offset(vma);
		fence->size = vma->fence_size;
		fence->stride = i915_gem_object_get_stride(vma->obj);
		fence->tiling = i915_gem_object_get_tiling(vma->obj);
	}
	WRITE_ONCE(fence->dirty, false);

	old = xchg(&fence->vma, NULL);
	if (old) {
		/* XXX Ideally we would move the waiting to outside the mutex */
		ret = i915_active_wait(&fence->active);
		if (ret) {
			fence->vma = old;
			return ret;
		}

		i915_vma_flush_writes(old);

		/*
		 * Ensure that all userspace CPU access is completed before
		 * stealing the fence.
		 */
		if (old != vma) {
			GEM_BUG_ON(old->fence != fence);
			i915_vma_revoke_mmap(old);
			old->fence = NULL;
		}

		list_move(&fence->link, &ggtt->fence_list);
	}

	/*
	 * We only need to update the register itself if the device is awake.
	 * If the device is currently powered down, we will defer the write
	 * to the runtime resume, see intel_ggtt_restore_fences().
	 *
	 * This only works for removing the fence register, on acquisition
	 * the caller must hold the rpm wakeref. The fence register must
	 * be cleared before we can use any other fences to ensure that
	 * the new fences do not overlap the elided clears, confusing HW.
	 */
	wakeref = intel_runtime_pm_get_if_in_use(uncore->rpm);
	if (!wakeref) {
		GEM_BUG_ON(vma);
		return 0;
	}

	WRITE_ONCE(fence->vma, vma);
	fence_write(fence);

	if (vma) {
		vma->fence = fence;
		list_move_tail(&fence->link, &ggtt->fence_list);
	}

	intel_runtime_pm_put(uncore->rpm, wakeref);
	return 0;
}

/**
 * i915_vma_revoke_fence - force-remove fence for a VMA
 * @vma: vma to map linearly (not through a fence reg)
 *
 * This function force-removes any fence from the given object, which is useful
 * if the kernel wants to do untiled GTT access.
 */
void i915_vma_revoke_fence(struct i915_vma *vma)
{
	struct i915_fence_reg *fence = vma->fence;
	intel_wakeref_t wakeref;

	lockdep_assert_held(&vma->vm->mutex);
	if (!fence)
		return;

	GEM_BUG_ON(fence->vma != vma);
	i915_active_wait(&fence->active);
	GEM_BUG_ON(!i915_active_is_idle(&fence->active));
	GEM_BUG_ON(atomic_read(&fence->pin_count));

	fence->tiling = 0;
	WRITE_ONCE(fence->vma, NULL);
	vma->fence = NULL;

	/*
	 * Skip the write to HW if and only if the device is currently
	 * suspended.
	 *
	 * If the driver does not currently hold a wakeref (if_in_use == 0),
	 * the device may currently be runtime suspended, or it may be woken
	 * up before the suspend takes place. If the device is not suspended
	 * (powered down) and we skip clearing the fence register, the HW is
	 * left in an undefined state where we may end up with multiple
	 * registers overlapping.
	 */
	with_intel_runtime_pm_if_active(fence_to_uncore(fence)->rpm, wakeref)
		fence_write(fence);
}

static bool fence_is_active(const struct i915_fence_reg *fence)
{
	return fence->vma && i915_vma_is_active(fence->vma);
}

static struct i915_fence_reg *fence_find(struct i915_ggtt *ggtt)
{
	struct i915_fence_reg *active = NULL;
	struct i915_fence_reg *fence, *fn;

	list_for_each_entry_safe(fence, fn, &ggtt->fence_list, link) {
		GEM_BUG_ON(fence->vma && fence->vma->fence != fence);

		if (fence == active) /* now seen this fence twice */
			active = ERR_PTR(-EAGAIN);

		/* Prefer idle fences so we do not have to wait on the GPU */
		if (active != ERR_PTR(-EAGAIN) && fence_is_active(fence)) {
			if (!active)
				active = fence;

			list_move_tail(&fence->link, &ggtt->fence_list);
			continue;
		}

		if (atomic_read(&fence->pin_count))
			continue;

		return fence;
	}

	/* Wait for completion of pending flips which consume fences */
	if (intel_has_pending_fb_unpin(ggtt->vm.i915))
		return ERR_PTR(-EAGAIN);

	return ERR_PTR(-ENOBUFS);
}

int __i915_vma_pin_fence(struct i915_vma *vma)
{
	struct i915_ggtt *ggtt = i915_vm_to_ggtt(vma->vm);
	struct i915_fence_reg *fence;
	struct i915_vma *set = i915_gem_object_is_tiled(vma->obj) ? vma : NULL;
	int err;

	lockdep_assert_held(&vma->vm->mutex);

	/* Just update our place in the LRU if our fence is getting reused. */
	if (vma->fence) {
		fence = vma->fence;
		GEM_BUG_ON(fence->vma != vma);
		atomic_inc(&fence->pin_count);
		if (!fence->dirty) {
			list_move_tail(&fence->link, &ggtt->fence_list);
			return 0;
		}
	} else if (set) {
		fence = fence_find(ggtt);
		if (IS_ERR(fence))
			return PTR_ERR(fence);

		GEM_BUG_ON(atomic_read(&fence->pin_count));
		atomic_inc(&fence->pin_count);
	} else {
		return 0;
	}

	err = fence_update(fence, set);
	if (err)
		goto out_unpin;

	GEM_BUG_ON(fence->vma != set);
	GEM_BUG_ON(vma->fence != (set ? fence : NULL));

	if (set)
		return 0;

out_unpin:
	atomic_dec(&fence->pin_count);
	return err;
}

/**
 * i915_vma_pin_fence - set up fencing for a vma
 * @vma: vma to map through a fence reg
 *
 * When mapping objects through the GTT, userspace wants to be able to write
 * to them without having to worry about swizzling if the object is tiled.
 * This function walks the fence regs looking for a free one for @obj,
 * stealing one if it can't find any.
 *
 * It then sets up the reg based on the object's properties: address, pitch
 * and tiling format.
 *
 * For an untiled surface, this removes any existing fence.
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
int i915_vma_pin_fence(struct i915_vma *vma)
{
	int err;

	if (!vma->fence && !i915_gem_object_is_tiled(vma->obj))
		return 0;

	/*
	 * Note that we revoke fences on runtime suspend. Therefore the user
	 * must keep the device awake whilst using the fence.
	 */
	assert_rpm_wakelock_held(vma->vm->gt->uncore->rpm);
	GEM_BUG_ON(!i915_vma_is_ggtt(vma));

	err = mutex_lock_interruptible(&vma->vm->mutex);
	if (err)
		return err;

	err = __i915_vma_pin_fence(vma);
	mutex_unlock(&vma->vm->mutex);

	return err;
}

/**
 * i915_reserve_fence - Reserve a fence for vGPU
 * @ggtt: Global GTT
 *
 * This function walks the fence regs looking for a free one and remove
 * it from the fence_list. It is used to reserve fence for vGPU to use.
 */
struct i915_fence_reg *i915_reserve_fence(struct i915_ggtt *ggtt)
{
	struct i915_fence_reg *fence;
	int count;
	int ret;

	lockdep_assert_held(&ggtt->vm.mutex);

	/* Keep at least one fence available for the display engine. */
	count = 0;
	list_for_each_entry(fence, &ggtt->fence_list, link)
		count += !atomic_read(&fence->pin_count);
	if (count <= 1)
		return ERR_PTR(-ENOSPC);

	fence = fence_find(ggtt);
	if (IS_ERR(fence))
		return fence;

	if (fence->vma) {
		/* Force-remove fence from VMA */
		ret = fence_update(fence, NULL);
		if (ret)
			return ERR_PTR(ret);
	}

	list_del(&fence->link);

	return fence;
}

/**
 * i915_unreserve_fence - Reclaim a reserved fence
 * @fence: the fence reg
 *
 * This function add a reserved fence register from vGPU to the fence_list.
 */
void i915_unreserve_fence(struct i915_fence_reg *fence)
{
	struct i915_ggtt *ggtt = fence->ggtt;

	lockdep_assert_held(&ggtt->vm.mutex);

	list_add(&fence->link, &ggtt->fence_list);
}

/**
 * intel_ggtt_restore_fences - restore fence state
 * @ggtt: Global GTT
 *
 * Restore the hw fence state to match the software tracking again, to be called
 * after a gpu reset and on resume. Note that on runtime suspend we only cancel
 * the fences, to be reacquired by the user later.
 */
void intel_ggtt_restore_fences(struct i915_ggtt *ggtt)
{
	int i;

	for (i = 0; i < ggtt->num_fences; i++)
		fence_write(&ggtt->fence_regs[i]);
}

/**
 * DOC: tiling swizzling details
 *
 * The idea behind tiling is to increase cache hit rates by rearranging
 * pixel data so that a group of pixel accesses are in the same cacheline.
 * Performance improvement from doing this on the back/depth buffer are on
 * the order of 30%.
 *
 * Intel architectures make this somewhat more complicated, though, by
 * adjustments made to addressing of data when the memory is in interleaved
 * mode (matched pairs of DIMMS) to improve memory bandwidth.
 * For interleaved memory, the CPU sends every sequential 64 bytes
 * to an alternate memory channel so it can get the bandwidth from both.
 *
 * The GPU also rearranges its accesses for increased bandwidth to interleaved
 * memory, and it matches what the CPU does for non-tiled.  However, when tiled
 * it does it a little differently, since one walks addresses not just in the
 * X direction but also Y.  So, along with alternating channels when bit
 * 6 of the address flips, it also alternates when other bits flip --  Bits 9
 * (every 512 bytes, an X tile scanline) and 10 (every two X tile scanlines)
 * are common to both the 915 and 965-class hardware.
 *
 * The CPU also sometimes XORs in higher bits as well, to improve
 * bandwidth doing strided access like we do so frequently in graphics.  This
 * is called "Channel XOR Randomization" in the MCH documentation.  The result
 * is that the CPU is XORing in either bit 11 or bit 17 to bit 6 of its address
 * decode.
 *
 * All of this bit 6 XORing has an effect on our memory management,
 * as we need to make sure that the 3d driver can correctly address object
 * contents.
 *
 * If we don't have interleaved memory, all tiling is safe and no swizzling is
 * required.
 *
 * When bit 17 is XORed in, we simply refuse to tile at all.  Bit
 * 17 is not just a page offset, so as we page an object out and back in,
 * individual pages in it will have different bit 17 addresses, resulting in
 * each 64 bytes being swapped with its neighbor!
 *
 * Otherwise, if interleaved, we have to tell the 3d driver what the address
 * swizzling it needs to do is, since it's writing with the CPU to the pages
 * (bit 6 and potentially bit 11 XORed in), and the GPU is reading from the
 * pages (bit 6, 9, and 10 XORed in), resulting in a cumulative bit swizzling
 * required by the CPU of XORing in bit 6, 9, 10, and potentially 11, in order
 * to match what the GPU expects.
 */

/**
 * detect_bit_6_swizzle - detect bit 6 swizzling pattern
 * @ggtt: Global GGTT
 *
 * Detects bit 6 swizzling of address lookup between IGD access and CPU
 * access through main memory.
 */
static void detect_bit_6_swizzle(struct i915_ggtt *ggtt)
{
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;
	struct drm_i915_private *i915 = ggtt->vm.i915;
	u32 swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
	u32 swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;

	if (GRAPHICS_VER(i915) >= 8 || IS_VALLEYVIEW(i915)) {
		/*
		 * On BDW+, swizzling is not used. We leave the CPU memory
		 * controller in charge of optimizing memory accesses without
		 * the extra address manipulation GPU side.
		 *
		 * VLV and CHV don't have GPU swizzling.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_NONE;
		swizzle_y = I915_BIT_6_SWIZZLE_NONE;
	} else if (GRAPHICS_VER(i915) >= 6) {
		if (i915->preserve_bios_swizzle) {
			if (intel_uncore_read(uncore, DISP_ARB_CTL) &
			    DISP_TILE_SURFACE_SWIZZLING) {
				swizzle_x = I915_BIT_6_SWIZZLE_9_10;
				swizzle_y = I915_BIT_6_SWIZZLE_9;
			} else {
				swizzle_x = I915_BIT_6_SWIZZLE_NONE;
				swizzle_y = I915_BIT_6_SWIZZLE_NONE;
			}
		} else {
			u32 dimm_c0, dimm_c1;

			dimm_c0 = intel_uncore_read(uncore, MAD_DIMM_C0);
			dimm_c1 = intel_uncore_read(uncore, MAD_DIMM_C1);
			dimm_c0 &= MAD_DIMM_A_SIZE_MASK | MAD_DIMM_B_SIZE_MASK;
			dimm_c1 &= MAD_DIMM_A_SIZE_MASK | MAD_DIMM_B_SIZE_MASK;
			/*
			 * Enable swizzling when the channels are populated
			 * with identically sized dimms. We don't need to check
			 * the 3rd channel because no cpu with gpu attached
			 * ships in that configuration. Also, swizzling only
			 * makes sense for 2 channels anyway.
			 */
			if (dimm_c0 == dimm_c1) {
				swizzle_x = I915_BIT_6_SWIZZLE_9_10;
				swizzle_y = I915_BIT_6_SWIZZLE_9;
			} else {
				swizzle_x = I915_BIT_6_SWIZZLE_NONE;
				swizzle_y = I915_BIT_6_SWIZZLE_NONE;
			}
		}
	} else if (GRAPHICS_VER(i915) == 5) {
		/*
		 * On Ironlake whatever DRAM config, GPU always do
		 * same swizzling setup.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_9_10;
		swizzle_y = I915_BIT_6_SWIZZLE_9;
	} else if (GRAPHICS_VER(i915) == 2) {
		/*
		 * As far as we know, the 865 doesn't have these bit 6
		 * swizzling issues.
		 */
		swizzle_x = I915_BIT_6_SWIZZLE_NONE;
		swizzle_y = I915_BIT_6_SWIZZLE_NONE;
	} else if (IS_G45(i915) || IS_I965G(i915) || IS_G33(i915)) {
		/*
		 * The 965, G33, and newer, have a very flexible memory
		 * configuration.  It will enable dual-channel mode
		 * (interleaving) on as much memory as it can, and the GPU
		 * will additionally sometimes enable different bit 6
		 * swizzling for tiled objects from the CPU.
		 *
		 * Here's what I found on the G965:
		 *    slot fill         memory size  swizzling
		 * 0A   0B   1A   1B    1-ch   2-ch
		 * 512  0    0    0     512    0     O
		 * 512  0    512  0     16     1008  X
		 * 512  0    0    512   16     1008  X
		 * 0    512  0    512   16     1008  X
		 * 1024 1024 1024 0     2048   1024  O
		 *
		 * We could probably detect this based on either the DRB
		 * matching, which was the case for the swizzling required in
		 * the table above, or from the 1-ch value being less than
		 * the minimum size of a rank.
		 *
		 * Reports indicate that the swizzling actually
		 * varies depending upon page placement inside the
		 * channels, i.e. we see swizzled pages where the
		 * banks of memory are paired and unswizzled on the
		 * uneven portion, so leave that as unknown.
		 */
		if (intel_uncore_read16(uncore, C0DRB3_BW) ==
		    intel_uncore_read16(uncore, C1DRB3_BW)) {
			swizzle_x = I915_BIT_6_SWIZZLE_9_10;
			swizzle_y = I915_BIT_6_SWIZZLE_9;
		}
	} else {
		u32 dcc = intel_uncore_read(uncore, DCC);

		/*
		 * On 9xx chipsets, channel interleave by the CPU is
		 * determined by DCC.  For single-channel, neither the CPU
		 * nor the GPU do swizzling.  For dual channel interleaved,
		 * the GPU's interleave is bit 9 and 10 for X tiled, and bit
		 * 9 for Y tiled.  The CPU's interleave is independent, and
		 * can be based on either bit 11 (haven't seen this yet) or
		 * bit 17 (common).
		 */
		switch (dcc & DCC_ADDRESSING_MODE_MASK) {
		case DCC_ADDRESSING_MODE_SINGLE_CHANNEL:
		case DCC_ADDRESSING_MODE_DUAL_CHANNEL_ASYMMETRIC:
			swizzle_x = I915_BIT_6_SWIZZLE_NONE;
			swizzle_y = I915_BIT_6_SWIZZLE_NONE;
			break;
		case DCC_ADDRESSING_MODE_DUAL_CHANNEL_INTERLEAVED:
			if (dcc & DCC_CHANNEL_XOR_DISABLE) {
				/*
				 * This is the base swizzling by the GPU for
				 * tiled buffers.
				 */
				swizzle_x = I915_BIT_6_SWIZZLE_9_10;
				swizzle_y = I915_BIT_6_SWIZZLE_9;
			} else if ((dcc & DCC_CHANNEL_XOR_BIT_17) == 0) {
				/* Bit 11 swizzling by the CPU in addition. */
				swizzle_x = I915_BIT_6_SWIZZLE_9_10_11;
				swizzle_y = I915_BIT_6_SWIZZLE_9_11;
			} else {
				/* Bit 17 swizzling by the CPU in addition. */
				swizzle_x = I915_BIT_6_SWIZZLE_9_10_17;
				swizzle_y = I915_BIT_6_SWIZZLE_9_17;
			}
			break;
		}

		/* check for L-shaped memory aka modified enhanced addressing */
		if (GRAPHICS_VER(i915) == 4 &&
		    !(intel_uncore_read(uncore, DCC2) & DCC2_MODIFIED_ENHANCED_DISABLE)) {
			swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
			swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;
		}

		if (dcc == 0xffffffff) {
			drm_err(&i915->drm, "Couldn't read from MCHBAR.  "
				  "Disabling tiling.\n");
			swizzle_x = I915_BIT_6_SWIZZLE_UNKNOWN;
			swizzle_y = I915_BIT_6_SWIZZLE_UNKNOWN;
		}
	}

	if (swizzle_x == I915_BIT_6_SWIZZLE_UNKNOWN ||
	    swizzle_y == I915_BIT_6_SWIZZLE_UNKNOWN) {
		/*
		 * Userspace likes to explode if it sees unknown swizzling,
		 * so lie. We will finish the lie when reporting through
		 * the get-tiling-ioctl by reporting the physical swizzle
		 * mode as unknown instead.
		 *
		 * As we don't strictly know what the swizzling is, it may be
		 * bit17 dependent, and so we need to also prevent the pages
		 * from being moved.
		 */
		i915->gem_quirks |= GEM_QUIRK_PIN_SWIZZLED_PAGES;
		swizzle_x = I915_BIT_6_SWIZZLE_NONE;
		swizzle_y = I915_BIT_6_SWIZZLE_NONE;
	}

	to_gt(i915)->ggtt->bit_6_swizzle_x = swizzle_x;
	to_gt(i915)->ggtt->bit_6_swizzle_y = swizzle_y;
}

/*
 * Swap every 64 bytes of this page around, to account for it having a new
 * bit 17 of its physical address and therefore being interpreted differently
 * by the GPU.
 */
static void swizzle_page(struct vm_page *page)
{
	char temp[64];
	char *vaddr;
	int i;

	vaddr = kmap(page);

	for (i = 0; i < PAGE_SIZE; i += 128) {
		memcpy(temp, &vaddr[i], 64);
		memcpy(&vaddr[i], &vaddr[i + 64], 64);
		memcpy(&vaddr[i + 64], temp, 64);
	}

#ifdef __linux__
	kunmap(page);
#else
	kunmap_va(vaddr);
#endif
}

/**
 * i915_gem_object_do_bit_17_swizzle - fixup bit 17 swizzling
 * @obj: i915 GEM buffer object
 * @pages: the scattergather list of physical pages
 *
 * This function fixes up the swizzling in case any page frame number for this
 * object has changed in bit 17 since that state has been saved with
 * i915_gem_object_save_bit_17_swizzle().
 *
 * This is called when pinning backing storage again, since the kernel is free
 * to move unpinned backing storage around (either by directly moving pages or
 * by swapping them out and back in again).
 */
void
i915_gem_object_do_bit_17_swizzle(struct drm_i915_gem_object *obj,
				  struct sg_table *pages)
{
	struct sgt_iter sgt_iter;
	struct vm_page *page;
	int i;

	if (obj->bit_17 == NULL)
		return;

	i = 0;
	for_each_sgt_page(page, sgt_iter, pages) {
		char new_bit_17 = page_to_phys(page) >> 17;

		if ((new_bit_17 & 0x1) != (test_bit(i, obj->bit_17) != 0)) {
			swizzle_page(page);
			set_page_dirty(page);
		}

		i++;
	}
}

/**
 * i915_gem_object_save_bit_17_swizzle - save bit 17 swizzling
 * @obj: i915 GEM buffer object
 * @pages: the scattergather list of physical pages
 *
 * This function saves the bit 17 of each page frame number so that swizzling
 * can be fixed up later on with i915_gem_object_do_bit_17_swizzle(). This must
 * be called before the backing storage can be unpinned.
 */
void
i915_gem_object_save_bit_17_swizzle(struct drm_i915_gem_object *obj,
				    struct sg_table *pages)
{
	const unsigned int page_count = obj->base.size >> PAGE_SHIFT;
	struct sgt_iter sgt_iter;
	struct vm_page *page;
	int i;

	if (obj->bit_17 == NULL) {
		obj->bit_17 = bitmap_zalloc(page_count, GFP_KERNEL);
		if (obj->bit_17 == NULL) {
			drm_err(obj->base.dev,
				"Failed to allocate memory for bit 17 record\n");
			return;
		}
	}

	i = 0;

	for_each_sgt_page(page, sgt_iter, pages) {
		if (page_to_phys(page) & (1 << 17))
			__set_bit(i, obj->bit_17);
		else
			__clear_bit(i, obj->bit_17);
		i++;
	}
}

void intel_ggtt_init_fences(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	struct intel_uncore *uncore = ggtt->vm.gt->uncore;
	int num_fences;
	int i;

	INIT_LIST_HEAD(&ggtt->fence_list);
	INIT_LIST_HEAD(&ggtt->userfault_list);

	detect_bit_6_swizzle(ggtt);

	if (!i915_ggtt_has_aperture(ggtt))
		num_fences = 0;
	else if (GRAPHICS_VER(i915) >= 7 &&
		 !(IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)))
		num_fences = 32;
	else if (GRAPHICS_VER(i915) >= 4 ||
		 IS_I945G(i915) || IS_I945GM(i915) ||
		 IS_G33(i915) || IS_PINEVIEW(i915))
		num_fences = 16;
	else
		num_fences = 8;

	if (intel_vgpu_active(i915))
		num_fences = intel_uncore_read(uncore,
					       vgtif_reg(avail_rs.fence_num));
	ggtt->fence_regs = kcalloc(num_fences,
				   sizeof(*ggtt->fence_regs),
				   GFP_KERNEL);
	if (!ggtt->fence_regs)
		num_fences = 0;

	/* Initialize fence registers to zero */
	for (i = 0; i < num_fences; i++) {
		struct i915_fence_reg *fence = &ggtt->fence_regs[i];

		i915_active_init(&fence->active, NULL, NULL, 0);
		fence->ggtt = ggtt;
		fence->id = i;
		list_add_tail(&fence->link, &ggtt->fence_list);
	}
	ggtt->num_fences = num_fences;

	intel_ggtt_restore_fences(ggtt);
}

void intel_ggtt_fini_fences(struct i915_ggtt *ggtt)
{
	int i;

	for (i = 0; i < ggtt->num_fences; i++) {
		struct i915_fence_reg *fence = &ggtt->fence_regs[i];

		i915_active_fini(&fence->active);
	}

	kfree(ggtt->fence_regs);
}

void intel_gt_init_swizzling(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;

	if (GRAPHICS_VER(i915) < 5 ||
	    to_gt(i915)->ggtt->bit_6_swizzle_x == I915_BIT_6_SWIZZLE_NONE)
		return;

	intel_uncore_rmw(uncore, DISP_ARB_CTL, 0, DISP_TILE_SURFACE_SWIZZLING);

	if (GRAPHICS_VER(i915) == 5)
		return;

	intel_uncore_rmw(uncore, TILECTL, 0, TILECTL_SWZCTL);

	if (GRAPHICS_VER(i915) == 6)
		intel_uncore_write(uncore,
				   ARB_MODE,
				   _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_SNB));
	else if (GRAPHICS_VER(i915) == 7)
		intel_uncore_write(uncore,
				   ARB_MODE,
				   _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_IVB));
	else if (GRAPHICS_VER(i915) == 8)
		intel_uncore_write(uncore,
				   GAMTARBMODE,
				   _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_BDW));
	else
		MISSING_CASE(GRAPHICS_VER(i915));
}
