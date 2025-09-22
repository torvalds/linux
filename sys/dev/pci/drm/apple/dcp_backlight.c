// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (C) The Asahi Linux Contributors */

#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_modeset_lock.h>

#include <linux/backlight.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include "linux/jiffies.h"

#include "dcp.h"
#include "dcp-internal.h"

#define MIN_BRIGHTNESS_PART1	2U
#define MAX_BRIGHTNESS_PART1	99U
#define MIN_BRIGHTNESS_PART2	103U
#define MAX_BRIGHTNESS_PART2	510U

/*
 * lookup for display brightness 2 to 99 nits
 * */
static u32 brightness_part1[] = {
	0x0000000, 0x0810038, 0x0f000bd, 0x143011c,
	0x1850165, 0x1bc01a1, 0x1eb01d4, 0x2140200,
	0x2380227, 0x2590249, 0x2770269, 0x2930285,
	0x2ac02a0, 0x2c402b8, 0x2d902cf, 0x2ee02e4,
	0x30102f8, 0x314030b, 0x325031c, 0x335032d,
	0x345033d, 0x354034d, 0x362035b, 0x3700369,
	0x37d0377, 0x38a0384, 0x3960390, 0x3a2039c,
	0x3ad03a7, 0x3b803b3, 0x3c303bd, 0x3cd03c8,
	0x3d703d2, 0x3e103dc, 0x3ea03e5, 0x3f303ef,
	0x3fc03f8, 0x4050400, 0x40d0409, 0x4150411,
	0x41d0419, 0x4250421, 0x42d0429, 0x4340431,
	0x43c0438, 0x443043f, 0x44a0446, 0x451044d,
	0x4570454, 0x45e045b, 0x4640461, 0x46b0468,
	0x471046e, 0x4770474, 0x47d047a, 0x4830480,
	0x4890486, 0x48e048b, 0x4940491, 0x4990497,
	0x49f049c, 0x4a404a1, 0x4a904a7, 0x4ae04ac,
	0x4b304b1, 0x4b804b6, 0x4bd04bb, 0x4c204c0,
	0x4c704c5, 0x4cc04c9, 0x4d004ce, 0x4d504d3,
	0x4d904d7, 0x4de04dc, 0x4e204e0, 0x4e704e4,
	0x4eb04e9, 0x4ef04ed, 0x4f304f1, 0x4f704f5,
	0x4fb04f9, 0x4ff04fd, 0x5030501, 0x5070505,
	0x50b0509, 0x50f050d, 0x5130511, 0x5160515,
	0x51a0518, 0x51e051c, 0x5210520, 0x5250523,
	0x5290527, 0x52c052a, 0x52f052e, 0x5330531,
	0x5360535, 0x53a0538, 0x53d053b, 0x540053f,
	0x5440542, 0x5470545, 0x54a0548, 0x54d054c,
	0x550054f, 0x5530552, 0x5560555, 0x5590558,
	0x55c055b, 0x55f055e, 0x5620561, 0x5650564,
	0x5680567, 0x56b056a, 0x56e056d, 0x571056f,
	0x5740572, 0x5760575, 0x5790578, 0x57c057b,
	0x57f057d, 0x5810580, 0x5840583, 0x5870585,
	0x5890588, 0x58c058b, 0x58f058d
};

static u32 brightness_part12[] = { 0x58f058d, 0x59d058f };

/*
 * lookup table for display brightness 103.3 to 510 nits
 * */
static u32 brightness_part2[] = {
	0x59d058f, 0x5b805ab, 0x5d105c5, 0x5e805dd,
	0x5fe05f3, 0x6120608, 0x625061c, 0x637062e,
	0x6480640, 0x6580650, 0x6680660, 0x677066f,
	0x685067e, 0x693068c, 0x6a00699, 0x6ac06a6,
	0x6b806b2, 0x6c406be, 0x6cf06ca, 0x6da06d5,
	0x6e506df, 0x6ef06ea, 0x6f906f4, 0x70206fe,
	0x70c0707, 0x7150710, 0x71e0719, 0x7260722,
	0x72f072a, 0x7370733, 0x73f073b, 0x7470743,
	0x74e074a, 0x7560752, 0x75d0759, 0x7640760,
	0x76b0768, 0x772076e, 0x7780775, 0x77f077c,
	0x7850782, 0x78c0789, 0x792078f, 0x7980795,
	0x79e079b, 0x7a407a1, 0x7aa07a7, 0x7af07ac,
	0x7b507b2, 0x7ba07b8, 0x7c007bd, 0x7c507c2,
	0x7ca07c8, 0x7cf07cd, 0x7d407d2, 0x7d907d7,
	0x7de07dc, 0x7e307e1, 0x7e807e5, 0x7ec07ea,
	0x7f107ef, 0x7f607f3, 0x7fa07f8, 0x7fe07fc
};


static int dcp_get_brightness(struct backlight_device *bd)
{
	struct apple_dcp *dcp = bl_get_data(bd);

	return dcp->brightness.nits;
}

#define SCALE_FACTOR (1 << 10)

static u32 interpolate(int val, int min, int max, u32 *tbl, size_t tbl_size)
{
	u32 frac;
	u64 low, high;
	u32 interpolated = (tbl_size - 1) * ((val - min) * SCALE_FACTOR) / (max - min);

	size_t index = interpolated / SCALE_FACTOR;

	if (WARN(index + 1 >= tbl_size, "invalid index %zu for brightness %u\n", index, val))
		return tbl[tbl_size / 2];

	frac = interpolated & (SCALE_FACTOR - 1);
	low = tbl[index];
	high = tbl[index + 1];

	return ((frac * high) + ((SCALE_FACTOR - frac) * low)) / SCALE_FACTOR;
}

static u32 calculate_dac(struct apple_dcp *dcp, int val)
{
	u32 dac;

	if (val <= MIN_BRIGHTNESS_PART1)
		return 16 * brightness_part1[0];
	else if (val == MAX_BRIGHTNESS_PART1)
		return 16 * brightness_part1[ARRAY_SIZE(brightness_part1) - 1];
	else if (val == MIN_BRIGHTNESS_PART2)
		return 16 * brightness_part2[0];
	else if (val >= MAX_BRIGHTNESS_PART2)
		return brightness_part2[ARRAY_SIZE(brightness_part2) - 1];

	if (val < MAX_BRIGHTNESS_PART1) {
		dac = interpolate(val, MIN_BRIGHTNESS_PART1, MAX_BRIGHTNESS_PART1,
				  brightness_part1, ARRAY_SIZE(brightness_part1));
	} else if (val > MIN_BRIGHTNESS_PART2) {
		dac = interpolate(val, MIN_BRIGHTNESS_PART2, MAX_BRIGHTNESS_PART2,
				  brightness_part2, ARRAY_SIZE(brightness_part2));
	} else {
		dac = interpolate(val, MAX_BRIGHTNESS_PART1, MIN_BRIGHTNESS_PART2,
				  brightness_part12, ARRAY_SIZE(brightness_part12));
	}

	return 16 * dac;
}

static int drm_crtc_set_brightness(struct apple_dcp *dcp)
{
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_crtc *crtc = &dcp->crtc->base;
	int ret = 0;

	DRM_MODESET_LOCK_ALL_BEGIN(crtc->dev, ctx, 0, ret);

	if (!dcp->brightness.update)
		goto done;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = &ctx;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto fail;
	}

	crtc_state->color_mgmt_changed |= true;

	ret = drm_atomic_commit(state);

fail:
	drm_atomic_state_put(state);
done:
	DRM_MODESET_LOCK_ALL_END(crtc->dev, ctx, ret);

	return ret;
}

int dcp_backlight_update(struct apple_dcp *dcp)
{
	/*
	 * Do not actively try to change brightness if no mode is set.
	 * TODO: should this be reflected the in backlight's power property?
	 *       defer this hopefully until it becomes irrelevant due to proper
	 *       drm integrated backlight handling
	 */
	if (!dcp->valid_mode)
		return 0;

	/* Wait 1 vblank cycle in the hope an atomic swap has already updated
	 * the brightness */
	drm_msleep((1001 + 23) / 24); // 42ms for 23.976 fps

	return drm_crtc_set_brightness(dcp);
}

static int dcp_set_brightness(struct backlight_device *bd)
{
	int ret = 0;
	struct apple_dcp *dcp = bl_get_data(bd);
	struct drm_modeset_acquire_ctx ctx;
	int brightness = backlight_get_brightness(bd);

	DRM_MODESET_LOCK_ALL_BEGIN(dcp->crtc->base.dev, ctx, 0, ret);

	dcp->brightness.dac = calculate_dac(dcp, brightness);
	dcp->brightness.update = true;

	DRM_MODESET_LOCK_ALL_END(dcp->crtc->base.dev, ctx, ret);

	return dcp_backlight_update(dcp);
}

static const struct backlight_ops dcp_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = dcp_get_brightness,
	.update_status = dcp_set_brightness,
};

int dcp_backlight_register(struct apple_dcp *dcp)
{
	struct device *dev = dcp->dev;
	struct backlight_device *bl_dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_PLATFORM,
		.brightness = dcp->brightness.nits,
		.scale = BACKLIGHT_SCALE_LINEAR,
	};
	props.max_brightness = min(dcp->brightness.maximum, MAX_BRIGHTNESS_PART2 - 1);

	bl_dev = devm_backlight_device_register(dev, "apple-panel-bl", dev, dcp,
						&dcp_backlight_ops, &props);
	if (IS_ERR(bl_dev))
		return PTR_ERR(bl_dev);

	dcp->brightness.bl_dev = bl_dev;
	dcp->brightness.dac = calculate_dac(dcp, dcp->brightness.nits);

	return 0;
}
