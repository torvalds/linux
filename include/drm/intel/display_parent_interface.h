/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation x*/

#ifndef __DISPLAY_PARENT_INTERFACE_H__
#define __DISPLAY_PARENT_INTERFACE_H__

#include <linux/types.h>

struct dma_fence;
struct drm_device;
struct drm_scanout_buffer;
struct intel_hdcp_gsc_context;
struct intel_panic;
struct intel_stolen_node;
struct ref_tracker;

/* Keep struct definitions sorted */

struct intel_display_hdcp_interface {
	ssize_t (*gsc_msg_send)(struct intel_hdcp_gsc_context *gsc_context,
				void *msg_in, size_t msg_in_len,
				void *msg_out, size_t msg_out_len);
	bool (*gsc_check_status)(struct drm_device *drm);
	struct intel_hdcp_gsc_context *(*gsc_context_alloc)(struct drm_device *drm);
	void (*gsc_context_free)(struct intel_hdcp_gsc_context *gsc_context);
};

struct intel_display_irq_interface {
	bool (*enabled)(struct drm_device *drm);
	void (*synchronize)(struct drm_device *drm);
};

struct intel_display_panic_interface {
	struct intel_panic *(*alloc)(void);
	int (*setup)(struct intel_panic *panic, struct drm_scanout_buffer *sb);
	void (*finish)(struct intel_panic *panic);
};

struct intel_display_pc8_interface {
	void (*block)(struct drm_device *drm);
	void (*unblock)(struct drm_device *drm);
};

struct intel_display_rpm_interface {
	struct ref_tracker *(*get)(const struct drm_device *drm);
	struct ref_tracker *(*get_raw)(const struct drm_device *drm);
	struct ref_tracker *(*get_if_in_use)(const struct drm_device *drm);
	struct ref_tracker *(*get_noresume)(const struct drm_device *drm);

	void (*put)(const struct drm_device *drm, struct ref_tracker *wakeref);
	void (*put_raw)(const struct drm_device *drm, struct ref_tracker *wakeref);
	void (*put_unchecked)(const struct drm_device *drm);

	bool (*suspended)(const struct drm_device *drm);
	void (*assert_held)(const struct drm_device *drm);
	void (*assert_block)(const struct drm_device *drm);
	void (*assert_unblock)(const struct drm_device *drm);
};

struct intel_display_rps_interface {
	void (*boost_if_not_started)(struct dma_fence *fence);
	void (*mark_interactive)(struct drm_device *drm, bool interactive);
	void (*ilk_irq_handler)(struct drm_device *drm);
};

struct intel_display_stolen_interface {
	int (*insert_node_in_range)(struct intel_stolen_node *node, u64 size,
				    unsigned int align, u64 start, u64 end);
	int (*insert_node)(struct intel_stolen_node *node, u64 size, unsigned int align); /* Optional */
	void (*remove_node)(struct intel_stolen_node *node);
	bool (*initialized)(struct drm_device *drm);
	bool (*node_allocated)(const struct intel_stolen_node *node);
	u64 (*node_offset)(const struct intel_stolen_node *node);
	u64 (*area_address)(struct drm_device *drm); /* Optional */
	u64 (*area_size)(struct drm_device *drm); /* Optional */
	u64 (*node_address)(const struct intel_stolen_node *node);
	u64 (*node_size)(const struct intel_stolen_node *node);
	struct intel_stolen_node *(*node_alloc)(struct drm_device *drm);
	void (*node_free)(const struct intel_stolen_node *node);
};

/**
 * struct intel_display_parent_interface - services parent driver provides to display
 *
 * The parent, or core, driver provides a pointer to this structure to display
 * driver when calling intel_display_device_probe(). The display driver uses it
 * to access services provided by the parent driver. The structure may contain
 * sub-struct pointers to group function pointers by functionality.
 *
 * All function and sub-struct pointers must be initialized and callable unless
 * explicitly marked as "optional" below. The display driver will only NULL
 * check the optional pointers.
 */
struct intel_display_parent_interface {
	/** @hdcp: HDCP GSC interface */
	const struct intel_display_hdcp_interface *hdcp;

	/** @irq: IRQ interface */
	const struct intel_display_irq_interface *irq;

	/** @panic: Panic interface */
	const struct intel_display_panic_interface *panic;

	/** @pc8: PC8 interface. Optional. */
	const struct intel_display_pc8_interface *pc8;

	/** @rpm: Runtime PM functions */
	const struct intel_display_rpm_interface *rpm;

	/** @rps: RPS interface. Optional. */
	const struct intel_display_rps_interface *rps;

	/** @stolen: Stolen memory. */
	const struct intel_display_stolen_interface *stolen;

	/* Generic independent functions */
	struct {
		/** @fence_priority_display: Set display priority. Optional. */
		void (*fence_priority_display)(struct dma_fence *fence);

		/** @has_auxccs: Are AuxCCS formats supported by the parent. Optional. */
		bool (*has_auxccs)(struct drm_device *drm);

		/** @has_fenced_regions: Support legacy fencing? Optional. */
		bool (*has_fenced_regions)(struct drm_device *drm);

		/** @vgpu_active: Is vGPU active? Optional. */
		bool (*vgpu_active)(struct drm_device *drm);
	};
};

#endif
