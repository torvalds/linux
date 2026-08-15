/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation x*/

#ifndef __DISPLAY_PARENT_INTERFACE_H__
#define __DISPLAY_PARENT_INTERFACE_H__

#include <linux/types.h>

enum vlv_iosf_sb_unit;
struct dma_fence;
struct drm_device;
struct drm_file;
struct drm_framebuffer;
struct drm_gem_object;
struct drm_mode_fb_cmd2;
struct drm_plane_state;
struct drm_scanout_buffer;
struct fb_info;
struct i915_gtt_view;
struct i915_vma;
struct intel_dpt;
struct intel_dsb_buffer;
struct intel_frontbuffer;
struct intel_hdcp_gsc_context;
struct intel_initial_plane_config;
struct intel_panic;
struct intel_stolen_node;
struct iosys_map;
struct ref_tracker;
struct seq_file;
struct vm_area_struct;

struct intel_fb_pin_params {
	const struct i915_gtt_view *view;
	unsigned int alignment;
	unsigned int phys_alignment;
	unsigned int vtd_guard;
	bool needs_cpu_lmem_access;
	bool needs_low_address;
	bool needs_physical;
	bool needs_fence;
};

/* Keep struct definitions sorted */

struct intel_display_bo_interface {
	bool (*is_tiled)(struct drm_gem_object *obj); /* Optional */
	bool (*is_userptr)(struct drm_gem_object *obj); /* Optional */
	bool (*is_shmem)(struct drm_gem_object *obj); /* Optional */
	bool (*is_protected)(struct drm_gem_object *obj);
	int (*key_check)(struct drm_gem_object *obj);
	int (*fb_mmap)(struct drm_gem_object *obj, struct vm_area_struct *vma);
	int (*read_from_page)(struct drm_gem_object *obj, u64 offset, void *dst, int size);
	void (*describe)(struct seq_file *m, struct drm_gem_object *obj); /* Optional */
	int (*framebuffer_init)(struct drm_gem_object *obj, struct drm_mode_fb_cmd2 *mode_cmd);
	void (*framebuffer_fini)(struct drm_gem_object *obj);
	struct drm_gem_object *(*framebuffer_lookup)(struct drm_device *drm,
						     struct drm_file *filp,
						     const struct drm_mode_fb_cmd2 *user_mode_cmd);
#if IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION)
	struct drm_gem_object *(*fbdev_create)(struct drm_device *drm, int size);
	void (*fbdev_destroy)(struct drm_gem_object *obj);
	int (*fbdev_fill_info)(struct drm_gem_object *obj, struct fb_info *info, struct i915_vma *vma);
	u32 (*fbdev_pitch_align)(u32 stride);
#endif
};

struct intel_display_dpt_interface {
	struct intel_dpt *(*create)(struct drm_gem_object *obj, size_t size);
	void (*destroy)(struct intel_dpt *dpt);
	void (*suspend)(struct intel_dpt *dpt);
	void (*resume)(struct intel_dpt *dpt);
};

struct intel_display_dsb_interface {
	u32 (*ggtt_offset)(struct intel_dsb_buffer *dsb_buf);
	void (*write)(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val);
	u32 (*read)(struct intel_dsb_buffer *dsb_buf, u32 idx);
	void (*fill)(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val, size_t size);
	struct intel_dsb_buffer *(*create)(struct drm_device *drm, size_t size);
	void (*cleanup)(struct intel_dsb_buffer *dsb_buf);
	void (*flush_map)(struct intel_dsb_buffer *dsb_buf);
};

struct intel_display_fb_pin_interface {
	int (*ggtt_pin)(struct drm_gem_object *obj,
			const struct intel_fb_pin_params *pin_params,
			struct i915_vma **out_ggtt_vma,
			u32 *out_offset,
			int *out_fence_id);
	void (*ggtt_unpin)(struct i915_vma *ggtt_vma,
			   int fence_id);
	int (*dpt_pin)(struct drm_gem_object *obj,
		       struct intel_dpt *dpt,
		       const struct intel_fb_pin_params *pin_params,
		       struct i915_vma **out_dpt_vma,
		       struct i915_vma **out_ggtt_vma,
		       u32 *out_offset);
	void (*dpt_unpin)(struct intel_dpt *dpt,
			  struct i915_vma *dpt_vma,
			  struct i915_vma *ggtt_vma);
	struct i915_vma *(*reuse_vma)(struct i915_vma *old_ggtt_vma,
				      struct drm_gem_object *old_obj,
				      const struct i915_gtt_view *old_view,
				      struct drm_gem_object *new_obj,
				      const struct i915_gtt_view *new_view,
				      u32 *out_offset);
	void (*get_map)(struct i915_vma *vma, struct iosys_map *map);
};

struct intel_display_frontbuffer_interface {
	struct intel_frontbuffer *(*get)(struct drm_gem_object *obj);
	void (*ref)(struct intel_frontbuffer *front);
	void (*put)(struct intel_frontbuffer *front);
	void (*flush_for_display)(struct intel_frontbuffer *front);
};

struct intel_display_hdcp_interface {
	ssize_t (*gsc_msg_send)(struct intel_hdcp_gsc_context *gsc_context,
				void *msg_in, size_t msg_in_len,
				void *msg_out, size_t msg_out_len);
	bool (*gsc_check_status)(struct drm_device *drm);
	struct intel_hdcp_gsc_context *(*gsc_context_alloc)(struct drm_device *drm);
	void (*gsc_context_free)(struct intel_hdcp_gsc_context *gsc_context);
};

struct intel_display_initial_plane_interface {
	struct drm_gem_object *(*alloc_obj)(struct drm_device *drm, struct intel_initial_plane_config *plane_config);
	int (*setup)(struct drm_plane_state *plane_state, struct intel_initial_plane_config *plane_config,
		     struct drm_framebuffer *fb, struct i915_vma *vma);
	void (*config_fini)(struct intel_initial_plane_config *plane_config);
};

struct intel_display_irq_interface {
	bool (*enabled)(struct drm_device *drm);
	void (*synchronize)(struct drm_device *drm);
};

struct intel_display_overlay_interface {
	bool (*is_active)(struct drm_device *drm);

	int (*overlay_on)(struct drm_device *drm,
			  u32 frontbuffer_bits);
	int (*overlay_continue)(struct drm_device *drm,
				struct i915_vma *vma,
				bool load_polyphase_filter);
	int (*overlay_off)(struct drm_device *drm);
	int (*recover_from_interrupt)(struct drm_device *drm);
	int (*release_old_vid)(struct drm_device *drm);

	void (*reset)(struct drm_device *drm);

	struct i915_vma *(*pin_fb)(struct drm_device *drm,
				   struct drm_gem_object *obj,
				   u32 *offset);
	void (*unpin_fb)(struct drm_device *drm,
			 struct i915_vma *vma);

	struct drm_gem_object *(*obj_lookup)(struct drm_device *drm,
					     struct drm_file *filp,
					     u32 handle);

	void __iomem *(*setup)(struct drm_device *drm,
			       bool needs_physical);
	void (*cleanup)(struct drm_device *drm);
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

struct intel_display_pcode_interface {
	int (*read)(struct drm_device *drm, u32 mbox, u32 *val, u32 *val1);
	int (*write)(struct drm_device *drm, u32 mbox, u32 val, int timeout_ms);
	int (*request)(struct drm_device *drm, u32 mbox, u32 request,
		       u32 reply_mask, u32 reply, int timeout_base_ms);
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

struct intel_display_vlv_iosf_interface {
	void (*get)(struct drm_device *drm, unsigned long unit_mask);
	void (*put)(struct drm_device *drm, unsigned long unit_mask);
	u32 (*read)(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr);
	int (*write)(struct drm_device *drm, enum vlv_iosf_sb_unit unit, u32 addr, u32 val);
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
	/** @bo: BO interface */
	const struct intel_display_bo_interface *bo;

	/** @dpt: DPT interface. Optional. */
	const struct intel_display_dpt_interface *dpt;

	/** @dsb: DSB buffer interface */
	const struct intel_display_dsb_interface *dsb;

	/** @fb_pin: Framebuffer pin interface */
	const struct intel_display_fb_pin_interface *fb_pin;

	/** @frontbuffer: Frontbuffer interface */
	const struct intel_display_frontbuffer_interface *frontbuffer;

	/** @hdcp: HDCP GSC interface */
	const struct intel_display_hdcp_interface *hdcp;

	/** @initial_plane: Initial plane interface */
	const struct intel_display_initial_plane_interface *initial_plane;

	/** @irq: IRQ interface */
	const struct intel_display_irq_interface *irq;

	/** @panic: Panic interface */
	const struct intel_display_panic_interface *panic;

	/** @overlay: Overlay. Optional. */
	const struct intel_display_overlay_interface *overlay;

	/** @pc8: PC8 interface. Optional. */
	const struct intel_display_pc8_interface *pc8;

	/** @pcode: Pcode interface */
	const struct intel_display_pcode_interface *pcode;

	/** @rpm: Runtime PM functions */
	const struct intel_display_rpm_interface *rpm;

	/** @rps: RPS interface. Optional. */
	const struct intel_display_rps_interface *rps;

	/** @stolen: Stolen memory. */
	const struct intel_display_stolen_interface *stolen;

	/** @vlv_iosf: VLV IOSF sideband. Optional. */
	const struct intel_display_vlv_iosf_interface *vlv_iosf;

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
