/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_MODE_CONFIG_H__
#define __DRM_MODE_CONFIG_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <linux/llist.h>

#include <drm/drm_modeset_lock.h>

struct drm_file;
struct drm_device;
struct drm_atomic_state;
struct drm_mode_fb_cmd2;
struct drm_format_info;
struct drm_display_mode;

/**
 * struct drm_mode_config_funcs - basic driver provided mode setting functions
 *
 * Some global (i.e. not per-CRTC, connector, etc) mode setting functions that
 * involve drivers.
 */
struct drm_mode_config_funcs {
	/**
	 * @fb_create:
	 *
	 * Create a new framebuffer object. The core does basic checks on the
	 * requested metadata, but most of that is left to the driver. See
	 * &struct drm_mode_fb_cmd2 for details.
	 *
	 * To validate the pixel format and modifier drivers can use
	 * drm_any_plane_has_format() to make sure at least one plane supports
	 * the requested values. Note that the driver must first determine the
	 * actual modifier used if the request doesn't have it specified,
	 * ie. when (@mode_cmd->flags & DRM_MODE_FB_MODIFIERS) == 0.
	 *
	 * IMPORTANT: These implied modifiers for legacy userspace must be
	 * stored in struct &drm_framebuffer, including all relevant metadata
	 * like &drm_framebuffer.pitches and &drm_framebuffer.offsets if the
	 * modifier enables additional planes beyond the fourcc pixel format
	 * code. This is required by the GETFB2 ioctl.
	 *
	 * If the parameters are deemed valid and the backing storage objects in
	 * the underlying memory manager all exist, then the driver allocates
	 * a new &drm_framebuffer structure, subclassed to contain
	 * driver-specific information (like the internal native buffer object
	 * references). It also needs to fill out all relevant metadata, which
	 * should be done by calling drm_helper_mode_fill_fb_struct().
	 *
	 * The initialization is finalized by calling drm_framebuffer_init(),
	 * which registers the framebuffer and makes it accessible to other
	 * threads.
	 *
	 * RETURNS:
	 *
	 * A new framebuffer with an initial reference count of 1 or a negative
	 * error code encoded with ERR_PTR().
	 */
	struct drm_framebuffer *(*fb_create)(struct drm_device *dev,
					     struct drm_file *file_priv,
					     const struct drm_mode_fb_cmd2 *mode_cmd);

	/**
	 * @get_format_info:
	 *
	 * Allows a driver to return custom format information for special
	 * fb layouts (eg. ones with auxiliary compression control planes).
	 *
	 * RETURNS:
	 *
	 * The format information specific to the given fb metadata, or
	 * NULL if none is found.
	 */
	const struct drm_format_info *(*get_format_info)(const struct drm_mode_fb_cmd2 *mode_cmd);

	/**
	 * @output_poll_changed:
	 *
	 * Callback used by helpers to inform the driver of output configuration
	 * changes.
	 *
	 * Drivers implementing fbdev emulation use drm_kms_helper_hotplug_event()
	 * to call this hook to inform the fbdev helper of output changes.
	 *
	 * This hook is deprecated, drivers should instead use
	 * drm_fbdev_generic_setup() which takes care of any necessary
	 * hotplug event forwarding already without further involvement by
	 * the driver.
	 */
	void (*output_poll_changed)(struct drm_device *dev);

	/**
	 * @mode_valid:
	 *
	 * Device specific validation of display modes. Can be used to reject
	 * modes that can never be supported. Only device wide constraints can
	 * be checked here. crtc/encoder/bridge/connector specific constraints
	 * should be checked in the .mode_valid() hook for each specific object.
	 */
	enum drm_mode_status (*mode_valid)(struct drm_device *dev,
					   const struct drm_display_mode *mode);

	/**
	 * @atomic_check:
	 *
	 * This is the only hook to validate an atomic modeset update. This
	 * function must reject any modeset and state changes which the hardware
	 * or driver doesn't support. This includes but is of course not limited
	 * to:
	 *
	 *  - Checking that the modes, framebuffers, scaling and placement
	 *    requirements and so on are within the limits of the hardware.
	 *
	 *  - Checking that any hidden shared resources are not oversubscribed.
	 *    This can be shared PLLs, shared lanes, overall memory bandwidth,
	 *    display fifo space (where shared between planes or maybe even
	 *    CRTCs).
	 *
	 *  - Checking that virtualized resources exported to userspace are not
	 *    oversubscribed. For various reasons it can make sense to expose
	 *    more planes, crtcs or encoders than which are physically there. One
	 *    example is dual-pipe operations (which generally should be hidden
	 *    from userspace if when lockstepped in hardware, exposed otherwise),
	 *    where a plane might need 1 hardware plane (if it's just on one
	 *    pipe), 2 hardware planes (when it spans both pipes) or maybe even
	 *    shared a hardware plane with a 2nd plane (if there's a compatible
	 *    plane requested on the area handled by the other pipe).
	 *
	 *  - Check that any transitional state is possible and that if
	 *    requested, the update can indeed be done in the vblank period
	 *    without temporarily disabling some functions.
	 *
	 *  - Check any other constraints the driver or hardware might have.
	 *
	 *  - This callback also needs to correctly fill out the &drm_crtc_state
	 *    in this update to make sure that drm_atomic_crtc_needs_modeset()
	 *    reflects the nature of the possible update and returns true if and
	 *    only if the update cannot be applied without tearing within one
	 *    vblank on that CRTC. The core uses that information to reject
	 *    updates which require a full modeset (i.e. blanking the screen, or
	 *    at least pausing updates for a substantial amount of time) if
	 *    userspace has disallowed that in its request.
	 *
	 *  - The driver also does not need to repeat basic input validation
	 *    like done for the corresponding legacy entry points. The core does
	 *    that before calling this hook.
	 *
	 * See the documentation of @atomic_commit for an exhaustive list of
	 * error conditions which don't have to be checked at the in this
	 * callback.
	 *
	 * See the documentation for &struct drm_atomic_state for how exactly
	 * an atomic modeset update is described.
	 *
	 * Drivers using the atomic helpers can implement this hook using
	 * drm_atomic_helper_check(), or one of the exported sub-functions of
	 * it.
	 *
	 * RETURNS:
	 *
	 * 0 on success or one of the below negative error codes:
	 *
	 *  - -EINVAL, if any of the above constraints are violated.
	 *
	 *  - -EDEADLK, when returned from an attempt to acquire an additional
	 *    &drm_modeset_lock through drm_modeset_lock().
	 *
	 *  - -ENOMEM, if allocating additional state sub-structures failed due
	 *    to lack of memory.
	 *
	 *  - -EINTR, -EAGAIN or -ERESTARTSYS, if the IOCTL should be restarted.
	 *    This can either be due to a pending signal, or because the driver
	 *    needs to completely bail out to recover from an exceptional
	 *    situation like a GPU hang. From a userspace point all errors are
	 *    treated equally.
	 */
	int (*atomic_check)(struct drm_device *dev,
			    struct drm_atomic_state *state);

	/**
	 * @atomic_commit:
	 *
	 * This is the only hook to commit an atomic modeset update. The core
	 * guarantees that @atomic_check has been called successfully before
	 * calling this function, and that nothing has been changed in the
	 * interim.
	 *
	 * See the documentation for &struct drm_atomic_state for how exactly
	 * an atomic modeset update is described.
	 *
	 * Drivers using the atomic helpers can implement this hook using
	 * drm_atomic_helper_commit(), or one of the exported sub-functions of
	 * it.
	 *
	 * Nonblocking commits (as indicated with the nonblock parameter) must
	 * do any preparatory work which might result in an unsuccessful commit
	 * in the context of this callback. The only exceptions are hardware
	 * errors resulting in -EIO. But even in that case the driver must
	 * ensure that the display pipe is at least running, to avoid
	 * compositors crashing when pageflips don't work. Anything else,
	 * specifically committing the update to the hardware, should be done
	 * without blocking the caller. For updates which do not require a
	 * modeset this must be guaranteed.
	 *
	 * The driver must wait for any pending rendering to the new
	 * framebuffers to complete before executing the flip. It should also
	 * wait for any pending rendering from other drivers if the underlying
	 * buffer is a shared dma-buf. Nonblocking commits must not wait for
	 * rendering in the context of this callback.
	 *
	 * An application can request to be notified when the atomic commit has
	 * completed. These events are per-CRTC and can be distinguished by the
	 * CRTC index supplied in &drm_event to userspace.
	 *
	 * The drm core will supply a &struct drm_event in each CRTC's
	 * &drm_crtc_state.event. See the documentation for
	 * &drm_crtc_state.event for more details about the precise semantics of
	 * this event.
	 *
	 * NOTE:
	 *
	 * Drivers are not allowed to shut down any display pipe successfully
	 * enabled through an atomic commit on their own. Doing so can result in
	 * compositors crashing if a page flip is suddenly rejected because the
	 * pipe is off.
	 *
	 * RETURNS:
	 *
	 * 0 on success or one of the below negative error codes:
	 *
	 *  - -EBUSY, if a nonblocking updated is requested and there is
	 *    an earlier updated pending. Drivers are allowed to support a queue
	 *    of outstanding updates, but currently no driver supports that.
	 *    Note that drivers must wait for preceding updates to complete if a
	 *    synchronous update is requested, they are not allowed to fail the
	 *    commit in that case.
	 *
	 *  - -ENOMEM, if the driver failed to allocate memory. Specifically
	 *    this can happen when trying to pin framebuffers, which must only
	 *    be done when committing the state.
	 *
	 *  - -ENOSPC, as a refinement of the more generic -ENOMEM to indicate
	 *    that the driver has run out of vram, iommu space or similar GPU
	 *    address space needed for framebuffer.
	 *
	 *  - -EIO, if the hardware completely died.
	 *
	 *  - -EINTR, -EAGAIN or -ERESTARTSYS, if the IOCTL should be restarted.
	 *    This can either be due to a pending signal, or because the driver
	 *    needs to completely bail out to recover from an exceptional
	 *    situation like a GPU hang. From a userspace point of view all errors are
	 *    treated equally.
	 *
	 * This list is exhaustive. Specifically this hook is not allowed to
	 * return -EINVAL (any invalid requests should be caught in
	 * @atomic_check) or -EDEADLK (this function must not acquire
	 * additional modeset locks).
	 */
	int (*atomic_commit)(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool nonblock);

	/**
	 * @atomic_state_alloc:
	 *
	 * This optional hook can be used by drivers that want to subclass struct
	 * &drm_atomic_state to be able to track their own driver-private global
	 * state easily. If this hook is implemented, drivers must also
	 * implement @atomic_state_clear and @atomic_state_free.
	 *
	 * Subclassing of &drm_atomic_state is deprecated in favour of using
	 * &drm_private_state and &drm_private_obj.
	 *
	 * RETURNS:
	 *
	 * A new &drm_atomic_state on success or NULL on failure.
	 */
	struct drm_atomic_state *(*atomic_state_alloc)(struct drm_device *dev);

	/**
	 * @atomic_state_clear:
	 *
	 * This hook must clear any driver private state duplicated into the
	 * passed-in &drm_atomic_state. This hook is called when the caller
	 * encountered a &drm_modeset_lock deadlock and needs to drop all
	 * already acquired locks as part of the deadlock avoidance dance
	 * implemented in drm_modeset_backoff().
	 *
	 * Any duplicated state must be invalidated since a concurrent atomic
	 * update might change it, and the drm atomic interfaces always apply
	 * updates as relative changes to the current state.
	 *
	 * Drivers that implement this must call drm_atomic_state_default_clear()
	 * to clear common state.
	 *
	 * Subclassing of &drm_atomic_state is deprecated in favour of using
	 * &drm_private_state and &drm_private_obj.
	 */
	void (*atomic_state_clear)(struct drm_atomic_state *state);

	/**
	 * @atomic_state_free:
	 *
	 * This hook needs driver private resources and the &drm_atomic_state
	 * itself. Note that the core first calls drm_atomic_state_clear() to
	 * avoid code duplicate between the clear and free hooks.
	 *
	 * Drivers that implement this must call
	 * drm_atomic_state_default_release() to release common resources.
	 *
	 * Subclassing of &drm_atomic_state is deprecated in favour of using
	 * &drm_private_state and &drm_private_obj.
	 */
	void (*atomic_state_free)(struct drm_atomic_state *state);
};

/**
 * struct drm_mode_config - Mode configuration control structure
 * @min_width: minimum fb pixel width on this device
 * @min_height: minimum fb pixel height on this device
 * @max_width: maximum fb pixel width on this device
 * @max_height: maximum fb pixel height on this device
 * @funcs: core driver provided mode setting functions
 * @poll_enabled: track polling support for this device
 * @poll_running: track polling status for this device
 * @delayed_event: track delayed poll uevent deliver for this device
 * @output_poll_work: delayed work for polling in process context
 * @preferred_depth: preferred RBG pixel depth, used by fb helpers
 * @prefer_shadow: hint to userspace to prefer shadow-fb rendering
 * @cursor_width: hint to userspace for max cursor width
 * @cursor_height: hint to userspace for max cursor height
 * @helper_private: mid-layer private data
 *
 * Core mode resource tracking structure.  All CRTC, encoders, and connectors
 * enumerated by the driver are added here, as are global properties.  Some
 * global restrictions are also here, e.g. dimension restrictions.
 *
 * Framebuffer sizes refer to the virtual screen that can be displayed by
 * the CRTC. This can be different from the physical resolution programmed.
 * The minimum width and height, stored in @min_width and @min_height,
 * describe the smallest size of the framebuffer. It correlates to the
 * minimum programmable resolution.
 * The maximum width, stored in @max_width, is typically limited by the
 * maximum pitch between two adjacent scanlines. The maximum height, stored
 * in @max_height, is usually only limited by the amount of addressable video
 * memory. For hardware that has no real maximum, drivers should pick a
 * reasonable default.
 *
 * See also @DRM_SHADOW_PLANE_MAX_WIDTH and @DRM_SHADOW_PLANE_MAX_HEIGHT.
 */
struct drm_mode_config {
	/**
	 * @mutex:
	 *
	 * This is the big scary modeset BKL which protects everything that
	 * isn't protect otherwise. Scope is unclear and fuzzy, try to remove
	 * anything from under its protection and move it into more well-scoped
	 * locks.
	 *
	 * The one important thing this protects is the use of @acquire_ctx.
	 */
	struct mutex mutex;

	/**
	 * @connection_mutex:
	 *
	 * This protects connector state and the connector to encoder to CRTC
	 * routing chain.
	 *
	 * For atomic drivers specifically this protects &drm_connector.state.
	 */
	struct drm_modeset_lock connection_mutex;

	/**
	 * @acquire_ctx:
	 *
	 * Global implicit acquire context used by atomic drivers for legacy
	 * IOCTLs. Deprecated, since implicit locking contexts make it
	 * impossible to use driver-private &struct drm_modeset_lock. Users of
	 * this must hold @mutex.
	 */
	struct drm_modeset_acquire_ctx *acquire_ctx;

	/**
	 * @idr_mutex:
	 *
	 * Mutex for KMS ID allocation and management. Protects both @object_idr
	 * and @tile_idr.
	 */
	struct mutex idr_mutex;

	/**
	 * @object_idr:
	 *
	 * Main KMS ID tracking object. Use this idr for all IDs, fb, crtc,
	 * connector, modes - just makes life easier to have only one.
	 */
	struct idr object_idr;

	/**
	 * @tile_idr:
	 *
	 * Use this idr for allocating new IDs for tiled sinks like use in some
	 * high-res DP MST screens.
	 */
	struct idr tile_idr;

	/** @fb_lock: Mutex to protect fb the global @fb_list and @num_fb. */
	struct mutex fb_lock;
	/** @num_fb: Number of entries on @fb_list. */
	int num_fb;
	/** @fb_list: List of all &struct drm_framebuffer. */
	struct list_head fb_list;

	/**
	 * @connector_list_lock: Protects @num_connector and
	 * @connector_list and @connector_free_list.
	 */
	spinlock_t connector_list_lock;
	/**
	 * @num_connector: Number of connectors on this device. Protected by
	 * @connector_list_lock.
	 */
	int num_connector;
	/**
	 * @connector_ida: ID allocator for connector indices.
	 */
	struct ida connector_ida;
	/**
	 * @connector_list:
	 *
	 * List of connector objects linked with &drm_connector.head. Protected
	 * by @connector_list_lock. Only use drm_for_each_connector_iter() and
	 * &struct drm_connector_list_iter to walk this list.
	 */
	struct list_head connector_list;
	/**
	 * @connector_free_list:
	 *
	 * List of connector objects linked with &drm_connector.free_head.
	 * Protected by @connector_list_lock. Used by
	 * drm_for_each_connector_iter() and
	 * &struct drm_connector_list_iter to savely free connectors using
	 * @connector_free_work.
	 */
	struct llist_head connector_free_list;
	/**
	 * @connector_free_work: Work to clean up @connector_free_list.
	 */
	struct work_struct connector_free_work;

	/**
	 * @num_encoder:
	 *
	 * Number of encoders on this device. This is invariant over the
	 * lifetime of a device and hence doesn't need any locks.
	 */
	int num_encoder;
	/**
	 * @encoder_list:
	 *
	 * List of encoder objects linked with &drm_encoder.head. This is
	 * invariant over the lifetime of a device and hence doesn't need any
	 * locks.
	 */
	struct list_head encoder_list;

	/**
	 * @num_total_plane:
	 *
	 * Number of universal (i.e. with primary/curso) planes on this device.
	 * This is invariant over the lifetime of a device and hence doesn't
	 * need any locks.
	 */
	int num_total_plane;
	/**
	 * @plane_list:
	 *
	 * List of plane objects linked with &drm_plane.head. This is invariant
	 * over the lifetime of a device and hence doesn't need any locks.
	 */
	struct list_head plane_list;

	/**
	 * @num_crtc:
	 *
	 * Number of CRTCs on this device linked with &drm_crtc.head. This is invariant over the lifetime
	 * of a device and hence doesn't need any locks.
	 */
	int num_crtc;
	/**
	 * @crtc_list:
	 *
	 * List of CRTC objects linked with &drm_crtc.head. This is invariant
	 * over the lifetime of a device and hence doesn't need any locks.
	 */
	struct list_head crtc_list;

	/**
	 * @property_list:
	 *
	 * List of property type objects linked with &drm_property.head. This is
	 * invariant over the lifetime of a device and hence doesn't need any
	 * locks.
	 */
	struct list_head property_list;

	/**
	 * @privobj_list:
	 *
	 * List of private objects linked with &drm_private_obj.head. This is
	 * invariant over the lifetime of a device and hence doesn't need any
	 * locks.
	 */
	struct list_head privobj_list;

	int min_width, min_height;
	int max_width, max_height;
	const struct drm_mode_config_funcs *funcs;

	/* output poll support */
	bool poll_enabled;
	bool poll_running;
	bool delayed_event;
	struct delayed_work output_poll_work;

	/**
	 * @blob_lock:
	 *
	 * Mutex for blob property allocation and management, protects
	 * @property_blob_list and &drm_file.blobs.
	 */
	struct mutex blob_lock;

	/**
	 * @property_blob_list:
	 *
	 * List of all the blob property objects linked with
	 * &drm_property_blob.head. Protected by @blob_lock.
	 */
	struct list_head property_blob_list;

	/* pointers to standard properties */

	/**
	 * @edid_property: Default connector property to hold the EDID of the
	 * currently connected sink, if any.
	 */
	struct drm_property *edid_property;
	/**
	 * @dpms_property: Default connector property to control the
	 * connector's DPMS state.
	 */
	struct drm_property *dpms_property;
	/**
	 * @path_property: Default connector property to hold the DP MST path
	 * for the port.
	 */
	struct drm_property *path_property;
	/**
	 * @tile_property: Default connector property to store the tile
	 * position of a tiled screen, for sinks which need to be driven with
	 * multiple CRTCs.
	 */
	struct drm_property *tile_property;
	/**
	 * @link_status_property: Default connector property for link status
	 * of a connector
	 */
	struct drm_property *link_status_property;
	/**
	 * @plane_type_property: Default plane property to differentiate
	 * CURSOR, PRIMARY and OVERLAY legacy uses of planes.
	 */
	struct drm_property *plane_type_property;
	/**
	 * @prop_src_x: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_x;
	/**
	 * @prop_src_y: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_y;
	/**
	 * @prop_src_w: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_w;
	/**
	 * @prop_src_h: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_h;
	/**
	 * @prop_crtc_x: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is being shown on.
	 */
	struct drm_property *prop_crtc_x;
	/**
	 * @prop_crtc_y: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is being shown on.
	 */
	struct drm_property *prop_crtc_y;
	/**
	 * @prop_crtc_w: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is being shown on.
	 */
	struct drm_property *prop_crtc_w;
	/**
	 * @prop_crtc_h: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is being shown on.
	 */
	struct drm_property *prop_crtc_h;
	/**
	 * @prop_fb_id: Default atomic plane property to specify the
	 * &drm_framebuffer.
	 */
	struct drm_property *prop_fb_id;
	/**
	 * @prop_in_fence_fd: Sync File fd representing the incoming fences
	 * for a Plane.
	 */
	struct drm_property *prop_in_fence_fd;
	/**
	 * @prop_out_fence_ptr: Sync File fd pointer representing the
	 * outgoing fences for a CRTC. Userspace should provide a pointer to a
	 * value of type s32, and then cast that pointer to u64.
	 */
	struct drm_property *prop_out_fence_ptr;
	/**
	 * @prop_crtc_id: Default atomic plane property to specify the
	 * &drm_crtc.
	 */
	struct drm_property *prop_crtc_id;
	/**
	 * @prop_fb_damage_clips: Optional plane property to mark damaged
	 * regions on the plane in framebuffer coordinates of the framebuffer
	 * attached to the plane.
	 *
	 * The layout of blob data is simply an array of &drm_mode_rect. Unlike
	 * plane src coordinates, damage clips are not in 16.16 fixed point.
	 */
	struct drm_property *prop_fb_damage_clips;
	/**
	 * @prop_active: Default atomic CRTC property to control the active
	 * state, which is the simplified implementation for DPMS in atomic
	 * drivers.
	 */
	struct drm_property *prop_active;
	/**
	 * @prop_mode_id: Default atomic CRTC property to set the mode for a
	 * CRTC. A 0 mode implies that the CRTC is entirely disabled - all
	 * connectors must be of and active must be set to disabled, too.
	 */
	struct drm_property *prop_mode_id;
	/**
	 * @prop_vrr_enabled: Default atomic CRTC property to indicate
	 * whether variable refresh rate should be enabled on the CRTC.
	 */
	struct drm_property *prop_vrr_enabled;

	/**
	 * @dvi_i_subconnector_property: Optional DVI-I property to
	 * differentiate between analog or digital mode.
	 */
	struct drm_property *dvi_i_subconnector_property;
	/**
	 * @dvi_i_select_subconnector_property: Optional DVI-I property to
	 * select between analog or digital mode.
	 */
	struct drm_property *dvi_i_select_subconnector_property;

	/**
	 * @dp_subconnector_property: Optional DP property to differentiate
	 * between different DP downstream port types.
	 */
	struct drm_property *dp_subconnector_property;

	/**
	 * @tv_subconnector_property: Optional TV property to differentiate
	 * between different TV connector types.
	 */
	struct drm_property *tv_subconnector_property;
	/**
	 * @tv_select_subconnector_property: Optional TV property to select
	 * between different TV connector types.
	 */
	struct drm_property *tv_select_subconnector_property;
	/**
	 * @tv_mode_property: Optional TV property to select
	 * the output TV mode.
	 */
	struct drm_property *tv_mode_property;
	/**
	 * @tv_left_margin_property: Optional TV property to set the left
	 * margin (expressed in pixels).
	 */
	struct drm_property *tv_left_margin_property;
	/**
	 * @tv_right_margin_property: Optional TV property to set the right
	 * margin (expressed in pixels).
	 */
	struct drm_property *tv_right_margin_property;
	/**
	 * @tv_top_margin_property: Optional TV property to set the right
	 * margin (expressed in pixels).
	 */
	struct drm_property *tv_top_margin_property;
	/**
	 * @tv_bottom_margin_property: Optional TV property to set the right
	 * margin (expressed in pixels).
	 */
	struct drm_property *tv_bottom_margin_property;
	/**
	 * @tv_brightness_property: Optional TV property to set the
	 * brightness.
	 */
	struct drm_property *tv_brightness_property;
	/**
	 * @tv_contrast_property: Optional TV property to set the
	 * contrast.
	 */
	struct drm_property *tv_contrast_property;
	/**
	 * @tv_flicker_reduction_property: Optional TV property to control the
	 * flicker reduction mode.
	 */
	struct drm_property *tv_flicker_reduction_property;
	/**
	 * @tv_overscan_property: Optional TV property to control the overscan
	 * setting.
	 */
	struct drm_property *tv_overscan_property;
	/**
	 * @tv_saturation_property: Optional TV property to set the
	 * saturation.
	 */
	struct drm_property *tv_saturation_property;
	/**
	 * @tv_hue_property: Optional TV property to set the hue.
	 */
	struct drm_property *tv_hue_property;

	/**
	 * @scaling_mode_property: Optional connector property to control the
	 * upscaling, mostly used for built-in panels.
	 */
	struct drm_property *scaling_mode_property;
	/**
	 * @aspect_ratio_property: Optional connector property to control the
	 * HDMI infoframe aspect ratio setting.
	 */
	struct drm_property *aspect_ratio_property;
	/**
	 * @content_type_property: Optional connector property to control the
	 * HDMI infoframe content type setting.
	 */
	struct drm_property *content_type_property;
	/**
	 * @degamma_lut_property: Optional CRTC property to set the LUT used to
	 * convert the framebuffer's colors to linear gamma.
	 */
	struct drm_property *degamma_lut_property;
	/**
	 * @degamma_lut_size_property: Optional CRTC property for the size of
	 * the degamma LUT as supported by the driver (read-only).
	 */
	struct drm_property *degamma_lut_size_property;
	/**
	 * @ctm_property: Optional CRTC property to set the
	 * matrix used to convert colors after the lookup in the
	 * degamma LUT.
	 */
	struct drm_property *ctm_property;
	/**
	 * @gamma_lut_property: Optional CRTC property to set the LUT used to
	 * convert the colors, after the CTM matrix, to the gamma space of the
	 * connected screen.
	 */
	struct drm_property *gamma_lut_property;
	/**
	 * @gamma_lut_size_property: Optional CRTC property for the size of the
	 * gamma LUT as supported by the driver (read-only).
	 */
	struct drm_property *gamma_lut_size_property;

	/**
	 * @suggested_x_property: Optional connector property with a hint for
	 * the position of the output on the host's screen.
	 */
	struct drm_property *suggested_x_property;
	/**
	 * @suggested_y_property: Optional connector property with a hint for
	 * the position of the output on the host's screen.
	 */
	struct drm_property *suggested_y_property;

	/**
	 * @non_desktop_property: Optional connector property with a hint
	 * that device isn't a standard display, and the console/desktop,
	 * should not be displayed on it.
	 */
	struct drm_property *non_desktop_property;

	/**
	 * @panel_orientation_property: Optional connector property indicating
	 * how the lcd-panel is mounted inside the casing (e.g. normal or
	 * upside-down).
	 */
	struct drm_property *panel_orientation_property;

	/**
	 * @writeback_fb_id_property: Property for writeback connectors, storing
	 * the ID of the output framebuffer.
	 * See also: drm_writeback_connector_init()
	 */
	struct drm_property *writeback_fb_id_property;

	/**
	 * @writeback_pixel_formats_property: Property for writeback connectors,
	 * storing an array of the supported pixel formats for the writeback
	 * engine (read-only).
	 * See also: drm_writeback_connector_init()
	 */
	struct drm_property *writeback_pixel_formats_property;
	/**
	 * @writeback_out_fence_ptr_property: Property for writeback connectors,
	 * fd pointer representing the outgoing fences for a writeback
	 * connector. Userspace should provide a pointer to a value of type s32,
	 * and then cast that pointer to u64.
	 * See also: drm_writeback_connector_init()
	 */
	struct drm_property *writeback_out_fence_ptr_property;

	/**
	 * @hdr_output_metadata_property: Connector property containing hdr
	 * metatada. This will be provided by userspace compositors based
	 * on HDR content
	 */
	struct drm_property *hdr_output_metadata_property;

	/**
	 * @content_protection_property: DRM ENUM property for content
	 * protection. See drm_connector_attach_content_protection_property().
	 */
	struct drm_property *content_protection_property;

	/**
	 * @hdcp_content_type_property: DRM ENUM property for type of
	 * Protected Content.
	 */
	struct drm_property *hdcp_content_type_property;

	/* dumb ioctl parameters */
	uint32_t preferred_depth, prefer_shadow;

	/**
	 * @prefer_shadow_fbdev:
	 *
	 * Hint to framebuffer emulation to prefer shadow-fb rendering.
	 */
	bool prefer_shadow_fbdev;

	/**
	 * @quirk_addfb_prefer_xbgr_30bpp:
	 *
	 * Special hack for legacy ADDFB to keep nouveau userspace happy. Should
	 * only ever be set by the nouveau kernel driver.
	 */
	bool quirk_addfb_prefer_xbgr_30bpp;

	/**
	 * @quirk_addfb_prefer_host_byte_order:
	 *
	 * When set to true drm_mode_addfb() will pick host byte order
	 * pixel_format when calling drm_mode_addfb2().  This is how
	 * drm_mode_addfb() should have worked from day one.  It
	 * didn't though, so we ended up with quirks in both kernel
	 * and userspace drivers to deal with the broken behavior.
	 * Simply fixing drm_mode_addfb() unconditionally would break
	 * these drivers, so add a quirk bit here to allow drivers
	 * opt-in.
	 */
	bool quirk_addfb_prefer_host_byte_order;

	/**
	 * @async_page_flip: Does this device support async flips on the primary
	 * plane?
	 */
	bool async_page_flip;

	/**
	 * @fb_modifiers_not_supported:
	 *
	 * When this flag is set, the DRM device will not expose modifier
	 * support to userspace. This is only used by legacy drivers that infer
	 * the buffer layout through heuristics without using modifiers. New
	 * drivers shall not set fhis flag.
	 */
	bool fb_modifiers_not_supported;

	/**
	 * @normalize_zpos:
	 *
	 * If true the drm core will call drm_atomic_normalize_zpos() as part of
	 * atomic mode checking from drm_atomic_helper_check()
	 */
	bool normalize_zpos;

	/**
	 * @modifiers_property: Plane property to list support modifier/format
	 * combination.
	 */
	struct drm_property *modifiers_property;

	/* cursor size */
	uint32_t cursor_width, cursor_height;

	/**
	 * @suspend_state:
	 *
	 * Atomic state when suspended.
	 * Set by drm_mode_config_helper_suspend() and cleared by
	 * drm_mode_config_helper_resume().
	 */
	struct drm_atomic_state *suspend_state;

	const struct drm_mode_config_helper_funcs *helper_private;
};

int __must_check drmm_mode_config_init(struct drm_device *dev);

/**
 * drm_mode_config_init - DRM mode_configuration structure initialization
 * @dev: DRM device
 *
 * This is the unmanaged version of drmm_mode_config_init() for drivers which
 * still explicitly call drm_mode_config_cleanup().
 *
 * FIXME: This function is deprecated and drivers should be converted over to
 * drmm_mode_config_init().
 */
static inline int drm_mode_config_init(struct drm_device *dev)
{
	return drmm_mode_config_init(dev);
}

void drm_mode_config_reset(struct drm_device *dev);
void drm_mode_config_cleanup(struct drm_device *dev);

#endif
