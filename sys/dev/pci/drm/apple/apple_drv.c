// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io> */
/* Based on meson driver which is
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 */

#include <linux/component.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_mode.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_fixed.h>

#include "dcp.h"

#define DRIVER_NAME     "apple"
#define DRIVER_DESC     "Apple display controller DRM driver"

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))

#define MAX_COPROCESSORS 2

struct apple_drm_private {
	struct drm_device drm;
};

DEFINE_DRM_GEM_DMA_FOPS(apple_fops);

#define DART_PAGE_SIZE 16384

static int apple_drm_gem_dumb_create(struct drm_file *file_priv,
                            struct drm_device *drm,
                            struct drm_mode_create_dumb *args)
{
        args->pitch = ALIGN(DIV_ROUND_UP(args->width * args->bpp, 8), 64);
        args->size = round_up(args->pitch * args->height, DART_PAGE_SIZE);

	return drm_gem_dma_dumb_create_internal(file_priv, drm, args);
}

static const struct drm_driver apple_drm_driver = {
	DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(apple_drm_gem_dumb_create),
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= "20221106",
	.major			= 1,
	.minor			= 0,
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops			= &apple_fops,
};

static int apple_plane_atomic_check(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state;
	struct drm_crtc_state *crtc_state;

	new_plane_state = drm_atomic_get_new_plane_state(state, plane);

	if (!new_plane_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state, new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	/*
	 * DCP limits downscaling to 2x and upscaling to 4x. Attempting to
	 * scale outside these bounds errors out when swapping.
	 *
	 * This function also takes care of clipping the src/dest rectangles,
	 * which is required for correct operation. Partially off-screen
	 * surfaces may appear corrupted.
	 *
	 * DCP does not distinguish plane types in the hardware, so we set
	 * can_position. If the primary plane does not fill the screen, the
	 * hardware will fill in zeroes (black).
	 */
	return drm_atomic_helper_check_plane_state(new_plane_state,
						   crtc_state,
						   FRAC_16_16(1, 4),
						   FRAC_16_16(2, 1),
						   true, true);
}

static void apple_plane_atomic_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	/* Handled in atomic_flush */
}

static const struct drm_plane_helper_funcs apple_plane_helper_funcs = {
	.atomic_check	= apple_plane_atomic_check,
	.atomic_update	= apple_plane_atomic_update,
};

static void apple_plane_cleanup(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
	kfree(plane);
}

static const struct drm_plane_funcs apple_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= apple_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

/*
 * Table of supported formats, mapping from DRM fourccs to DCP fourccs.
 *
 * For future work, DCP supports more formats not listed, including YUV
 * formats, an extra RGBA format, and a biplanar RGB10_A8 format (fourcc b3a8)
 * used for HDR.
 *
 * Note: we don't have non-alpha formats but userspace breaks without XRGB. It
 * doesn't matter for the primary plane, but cursors/overlays must not
 * advertise formats without alpha.
 */
static const u32 dcp_formats[] = {
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
};

u64 apple_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static struct drm_plane *apple_plane_init(struct drm_device *dev,
					  unsigned long possible_crtcs,
					  enum drm_plane_type type)
{
	int ret;
	struct drm_plane *plane;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);

	ret = drm_universal_plane_init(dev, plane, possible_crtcs,
				       &apple_plane_funcs,
				       dcp_formats, ARRAY_SIZE(dcp_formats),
				       apple_format_modifiers, type, NULL);
	if (ret)
		return ERR_PTR(ret);

	drm_plane_helper_add(plane, &apple_plane_helper_funcs);

	return plane;
}

static enum drm_connector_status
apple_connector_detect(struct drm_connector *connector, bool force)
{
	struct apple_connector *apple_connector = to_apple_connector(connector);

	return apple_connector->connected ? connector_status_connected :
						  connector_status_disconnected;
}

static void apple_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	if (crtc_state->active_changed && crtc_state->active) {
		struct apple_crtc *apple_crtc = to_apple_crtc(crtc);
		dcp_poweron(apple_crtc->dcp);
	}

	if (crtc_state->active)
		dcp_crtc_atomic_modeset(crtc, state);
}

static void apple_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	if (crtc_state->active_changed && !crtc_state->active) {
		struct apple_crtc *apple_crtc = to_apple_crtc(crtc);
		dcp_poweroff(apple_crtc->dcp);
	}

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static void apple_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct apple_crtc *apple_crtc = to_apple_crtc(crtc);
	unsigned long flags;

	if (crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		apple_crtc->event = crtc->state->event;
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		crtc->state->event = NULL;
	}
}

static void dcp_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);

	drm_atomic_helper_fake_vblank(old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_wait_for_flip_done(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static void apple_crtc_cleanup(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(to_apple_crtc(crtc));
}

static const struct drm_crtc_funcs apple_crtc_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.destroy		= apple_crtc_cleanup,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.set_config             = drm_atomic_helper_set_config,
};

static const struct drm_mode_config_funcs apple_mode_config_funcs = {
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
	.fb_create		= drm_gem_fb_create,
};

static const struct drm_mode_config_helper_funcs apple_mode_config_helpers = {
	.atomic_commit_tail	= dcp_atomic_commit_tail,
};

static void appledrm_connector_cleanup(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
	kfree(to_apple_connector(connector));
}

static const struct drm_connector_funcs apple_connector_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= appledrm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.detect			= apple_connector_detect,
};

static const struct drm_connector_helper_funcs apple_connector_helper_funcs = {
	.get_modes		= dcp_get_modes,
	.mode_valid		= dcp_mode_valid,
};

static const struct drm_crtc_helper_funcs apple_crtc_helper_funcs = {
	.atomic_begin		= apple_crtc_atomic_begin,
	.atomic_check		= dcp_crtc_atomic_check,
	.atomic_flush		= dcp_flush,
	.atomic_enable		= apple_crtc_atomic_enable,
	.atomic_disable		= apple_crtc_atomic_disable,
	.mode_fixup		= dcp_crtc_mode_fixup,
};

static int apple_probe_per_dcp(struct device *dev,
			       struct drm_device *drm,
			       struct platform_device *dcp,
			       int num, bool dcp_ext)
{
	struct apple_crtc *crtc;
	struct apple_connector *connector;
	struct apple_encoder *enc;
	struct drm_plane *primary;
	int ret;

	primary = apple_plane_init(drm, 1U << num, DRM_PLANE_TYPE_PRIMARY);

	if (IS_ERR(primary))
		return PTR_ERR(primary);

	crtc = kzalloc(sizeof(*crtc), GFP_KERNEL);
	ret = drm_crtc_init_with_planes(drm, &crtc->base, primary, NULL,
					&apple_crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(&crtc->base, &apple_crtc_helper_funcs);
	drm_crtc_enable_color_mgmt(&crtc->base, 0, true, 0);

	enc = drmm_simple_encoder_alloc(drm, struct apple_encoder, base,
					DRM_MODE_ENCODER_TMDS);
	if (IS_ERR(enc))
                return PTR_ERR(enc);
	enc->base.possible_crtcs = drm_crtc_mask(&crtc->base);

	connector = kzalloc(sizeof(*connector), GFP_KERNEL);
	drm_connector_helper_add(&connector->base,
				 &apple_connector_helper_funcs);

#ifdef __linux__
	// HACK:
	if (dcp_ext)
		connector->base.fwnode = fwnode_handle_get(dev->fwnode);
#endif

	ret = drm_connector_init(drm, &connector->base, &apple_connector_funcs,
				 dcp_get_connector_type(dcp));
	if (ret)
		return ret;

	connector->base.polled = DRM_CONNECTOR_POLL_HPD;
	connector->connected = false;
	connector->dcp = dcp;

	INIT_WORK(&connector->hotplug_wq, dcp_hotplug);

	crtc->dcp = dcp;
	dcp_link(dcp, crtc, connector);

	return drm_connector_attach_encoder(&connector->base, &enc->base);
}

static int apple_get_fb_resource(struct device *dev, const char *name,
				 struct resource *fb_r)
{
	int idx, ret = -ENODEV;
	struct device_node *node;

	idx = of_property_match_string(dev->of_node, "memory-region-names", name);

	node = of_parse_phandle(dev->of_node, "memory-region", idx);
	if (!node) {
		dev_err(dev, "reserved-memory node '%s' not found\n", name);
		return -ENODEV;
	}

	if (!of_device_is_available(node)) {
		dev_err(dev, "reserved-memory node '%s' is unavailable\n", name);
		goto err;
	}

	if (!of_device_is_compatible(node, "framebuffer")) {
		dev_err(dev, "reserved-memory node '%s' is incompatible\n",
			node->full_name);
		goto err;
	}

	ret = of_address_to_resource(node, 0, fb_r);

err:
	of_node_put(node);
	return ret;
}

static const struct of_device_id apple_dcp_id_tbl[] = {
	{ .compatible = "apple,dcp" },
	{ .compatible = "apple,dcpext" },
	{},
};

static int apple_drm_init_dcp(struct device *dev)
{
	struct apple_drm_private *apple = dev_get_drvdata(dev);
	struct platform_device *dcp[MAX_COPROCESSORS];
	struct device_node *np;
	u64 timeout;
	int i, ret, num_dcp = 0;

	for_each_matching_node(np, apple_dcp_id_tbl) {
		bool dcp_ext;
		if (!of_device_is_available(np)) {
			of_node_put(np);
			continue;
		}
		dcp_ext = of_device_is_compatible(np, "apple,dcpext");

		dcp[num_dcp] = of_find_device_by_node(np);
		of_node_put(np);
		if (!dcp[num_dcp])
			continue;

		ret = apple_probe_per_dcp(dev, &apple->drm, dcp[num_dcp],
					  num_dcp, dcp_ext);
		if (ret)
			continue;

		ret = dcp_start(dcp[num_dcp]);
		if (ret)
			continue;

		num_dcp++;
	}

	if (num_dcp < 1)
		return -ENODEV;

	/*
	 * Starting DPTX might take some time.
	 */
	timeout = get_jiffies_64() + msecs_to_jiffies(3000);

	for (i = 0; i < num_dcp; ++i) {
		u64 jiffies = get_jiffies_64();
		u64 wait = time_after_eq64(jiffies, timeout) ?
				   0 :
				   timeout - jiffies;
		ret = dcp_wait_ready(dcp[i], wait);
		/* There is nothing we can do if a dcp/dcpext does not boot
		 * (successfully). Ignoring it should not do any harm now.
		 * Needs to reevaluated when adding dcpext support.
		 */
		if (ret)
			dev_warn(dev, "DCP[%d] not ready: %d\n", i, ret);
	}
	/* HACK: Wait for dcp* to settle before a modeset */
	drm_msleep(100);

	return 0;
}

static int apple_drm_init(struct device *dev)
{
	struct apple_drm_private *apple;
	struct resource fb_r;
	resource_size_t fb_size;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(42));
	if (ret)
		return ret;

	ret = apple_get_fb_resource(dev, "framebuffer", &fb_r);
	if (ret)
		return ret;

	fb_size = fb_r.end - fb_r.start + 1;
	ret = drm_aperture_remove_conflicting_framebuffers(fb_r.start, fb_size,
						&apple_drm_driver);
	if (ret) {
		dev_err(dev, "Failed remove fb: %d\n", ret);
		goto err_unbind;
	}

#ifdef __linux__
	apple = devm_drm_dev_alloc(dev, &apple_drm_driver,
				   struct apple_drm_private, drm);
	if (IS_ERR(apple))
		return PTR_ERR(apple);
#else
	struct apldrm_softc *sc = (struct apldrm_softc *)dev;
	apple = (struct apple_drm_private *)&sc->sc_ddev;
#endif

	dev_set_drvdata(dev, apple);

	ret = component_bind_all(dev, apple);
	if (ret)
		return ret;

	ret = drmm_mode_config_init(&apple->drm);
	if (ret)
		goto err_unbind;

	/*
	 * IOMFB::UPPipeDCP_H13P::verify_surfaces produces the error "plane
	 * requires a minimum of 32x32 for the source buffer" if smaller
	 */
	apple->drm.mode_config.min_width = 32;
	apple->drm.mode_config.min_height = 32;

	/*
	 * TODO: this is the max framebuffer size not the maximal supported
	 * output resolution. DCP reports the maximal framebuffer size take it
	 * from there.
	 * Hardcode it for now to the M1 Max DCP reported 'MaxSrcBufferWidth'
	 * and 'MaxSrcBufferHeight' of 16384.
	 */
	apple->drm.mode_config.max_width = 16384;
	apple->drm.mode_config.max_height = 16384;

	apple->drm.mode_config.funcs = &apple_mode_config_funcs;
	apple->drm.mode_config.helper_private = &apple_mode_config_helpers;

	ret = apple_drm_init_dcp(dev);
	if (ret)
		goto err_unbind;

	drm_mode_config_reset(&apple->drm);

	ret = drm_dev_register(&apple->drm, 0);
	if (ret)
		goto err_unbind;

	drm_fbdev_dma_setup(&apple->drm, 32);

	return 0;

err_unbind:
	component_unbind_all(dev, NULL);
	return ret;
}

static void apple_drm_uninit(struct device *dev)
{
	struct apple_drm_private *apple = dev_get_drvdata(dev);

	drm_dev_unregister(&apple->drm);
	drm_atomic_helper_shutdown(&apple->drm);

	component_unbind_all(dev, NULL);

	dev_set_drvdata(dev, NULL);
}

static int apple_drm_bind(struct device *dev)
{
	return apple_drm_init(dev);
}

static void apple_drm_unbind(struct device *dev)
{
	apple_drm_uninit(dev);
}

const struct component_master_ops apple_drm_ops = {
	.bind	= apple_drm_bind,
	.unbind	= apple_drm_unbind,
};

static int add_dcp_components(struct device *dev,
			      struct component_match **matchptr)
{
	struct device_node *np;
	int num = 0;

	for_each_matching_node(np, apple_dcp_id_tbl) {
		if (of_device_is_available(np)) {
			drm_of_component_match_add(dev, matchptr,
						   component_compare_of, np);
			num++;
		}
		of_node_put(np);
	}

	return num;
}

static int apple_platform_probe(struct platform_device *pdev)
{
	struct device *mdev = &pdev->dev;
	struct component_match *match = NULL;
	int num_dcp;

	/* add DCP components, handle less than 1 as probe error */
	num_dcp = add_dcp_components(mdev, &match);
	if (num_dcp < 1)
		return -ENODEV;

	return component_master_add_with_match(mdev, &apple_drm_ops, match);
}

#ifdef __linux__

static int apple_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &apple_drm_ops);

	return 0;
}

static const struct of_device_id of_match[] = {
	{ .compatible = "apple,display-subsystem" },
	{}
};
MODULE_DEVICE_TABLE(of, of_match);

#endif

#ifdef CONFIG_PM_SLEEP
static int apple_platform_suspend(struct device *dev)
{
	struct apple_drm_private *apple = dev_get_drvdata(dev);

	if (apple)
		return drm_mode_config_helper_suspend(&apple->drm);

	return 0;
}

static int apple_platform_resume(struct device *dev)
{
	struct apple_drm_private *apple = dev_get_drvdata(dev);

	if (apple)
		drm_mode_config_helper_resume(&apple->drm);

	return 0;
}

static const struct dev_pm_ops apple_platform_pm_ops = {
	.suspend	= apple_platform_suspend,
	.resume		= apple_platform_resume,
};
#endif

#ifdef __linux__

static struct platform_driver apple_platform_driver = {
	.driver	= {
		.name = "apple-drm",
		.of_match_table	= of_match,
#ifdef CONFIG_PM_SLEEP
		.pm = &apple_platform_pm_ops,
#endif
	},
	.probe		= apple_platform_probe,
	.remove		= apple_platform_remove,
};

drm_module_platform_driver(apple_platform_driver);

MODULE_AUTHOR("Alyssa Rosenzweig <alyssa@rosenzweig.io>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual MIT/GPL");

#endif
