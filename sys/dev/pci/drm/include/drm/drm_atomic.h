/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#ifndef DRM_ATOMIC_H_
#define DRM_ATOMIC_H_

#include <drm/drm_crtc.h>
#include <drm/drm_util.h>

/**
 * struct drm_crtc_commit - track modeset commits on a CRTC
 *
 * This structure is used to track pending modeset changes and atomic commit on
 * a per-CRTC basis. Since updating the list should never block, this structure
 * is reference counted to allow waiters to safely wait on an event to complete,
 * without holding any locks.
 *
 * It has 3 different events in total to allow a fine-grained synchronization
 * between outstanding updates::
 *
 *	atomic commit thread			hardware
 *
 * 	write new state into hardware	---->	...
 * 	signal hw_done
 * 						switch to new state on next
 * 	...					v/hblank
 *
 *	wait for buffers to show up		...
 *
 *	...					send completion irq
 *						irq handler signals flip_done
 *	cleanup old buffers
 *
 * 	signal cleanup_done
 *
 * 	wait for flip_done		<----
 * 	clean up atomic state
 *
 * The important bit to know is that &cleanup_done is the terminal event, but the
 * ordering between &flip_done and &hw_done is entirely up to the specific driver
 * and modeset state change.
 *
 * For an implementation of how to use this look at
 * drm_atomic_helper_setup_commit() from the atomic helper library.
 *
 * See also drm_crtc_commit_wait().
 */
struct drm_crtc_commit {
	/**
	 * @crtc:
	 *
	 * DRM CRTC for this commit.
	 */
	struct drm_crtc *crtc;

	/**
	 * @ref:
	 *
	 * Reference count for this structure. Needed to allow blocking on
	 * completions without the risk of the completion disappearing
	 * meanwhile.
	 */
	struct kref ref;

	/**
	 * @flip_done:
	 *
	 * Will be signaled when the hardware has flipped to the new set of
	 * buffers. Signals at the same time as when the drm event for this
	 * commit is sent to userspace, or when an out-fence is singalled. Note
	 * that for most hardware, in most cases this happens after @hw_done is
	 * signalled.
	 *
	 * Completion of this stage is signalled implicitly by calling
	 * drm_crtc_send_vblank_event() on &drm_crtc_state.event.
	 */
	struct completion flip_done;

	/**
	 * @hw_done:
	 *
	 * Will be signalled when all hw register changes for this commit have
	 * been written out. Especially when disabling a pipe this can be much
	 * later than @flip_done, since that can signal already when the
	 * screen goes black, whereas to fully shut down a pipe more register
	 * I/O is required.
	 *
	 * Note that this does not need to include separately reference-counted
	 * resources like backing storage buffer pinning, or runtime pm
	 * management.
	 *
	 * Drivers should call drm_atomic_helper_commit_hw_done() to signal
	 * completion of this stage.
	 */
	struct completion hw_done;

	/**
	 * @cleanup_done:
	 *
	 * Will be signalled after old buffers have been cleaned up by calling
	 * drm_atomic_helper_cleanup_planes(). Since this can only happen after
	 * a vblank wait completed it might be a bit later. This completion is
	 * useful to throttle updates and avoid hardware updates getting ahead
	 * of the buffer cleanup too much.
	 *
	 * Drivers should call drm_atomic_helper_commit_cleanup_done() to signal
	 * completion of this stage.
	 */
	struct completion cleanup_done;

	/**
	 * @commit_entry:
	 *
	 * Entry on the per-CRTC &drm_crtc.commit_list. Protected by
	 * $drm_crtc.commit_lock.
	 */
	struct list_head commit_entry;

	/**
	 * @event:
	 *
	 * &drm_pending_vblank_event pointer to clean up private events.
	 */
	struct drm_pending_vblank_event *event;

	/**
	 * @abort_completion:
	 *
	 * A flag that's set after drm_atomic_helper_setup_commit() takes a
	 * second reference for the completion of $drm_crtc_state.event. It's
	 * used by the free code to remove the second reference if commit fails.
	 */
	bool abort_completion;
};

struct __drm_planes_state {
	struct drm_plane *ptr;
	struct drm_plane_state *state, *old_state, *new_state;
};

struct __drm_crtcs_state {
	struct drm_crtc *ptr;
	struct drm_crtc_state *state, *old_state, *new_state;

	/**
	 * @commit:
	 *
	 * A reference to the CRTC commit object that is kept for use by
	 * drm_atomic_helper_wait_for_flip_done() after
	 * drm_atomic_helper_commit_hw_done() is called. This ensures that a
	 * concurrent commit won't free a commit object that is still in use.
	 */
	struct drm_crtc_commit *commit;

	s32 __user *out_fence_ptr;
	u64 last_vblank_count;
};

struct __drm_connnectors_state {
	struct drm_connector *ptr;
	struct drm_connector_state *state, *old_state, *new_state;
	/**
	 * @out_fence_ptr:
	 *
	 * User-provided pointer which the kernel uses to return a sync_file
	 * file descriptor. Used by writeback connectors to signal completion of
	 * the writeback.
	 */
	s32 __user *out_fence_ptr;
};

struct drm_private_obj;
struct drm_private_state;

/**
 * struct drm_private_state_funcs - atomic state functions for private objects
 *
 * These hooks are used by atomic helpers to create, swap and destroy states of
 * private objects. The structure itself is used as a vtable to identify the
 * associated private object type. Each private object type that needs to be
 * added to the atomic states is expected to have an implementation of these
 * hooks and pass a pointer to its drm_private_state_funcs struct to
 * drm_atomic_get_private_obj_state().
 */
struct drm_private_state_funcs {
	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current state of the private object and return it. It
	 * is an error to call this before obj->state has been initialized.
	 *
	 * RETURNS:
	 *
	 * Duplicated atomic state or NULL when obj->state is not
	 * initialized or allocation failed.
	 */
	struct drm_private_state *(*atomic_duplicate_state)(struct drm_private_obj *obj);

	/**
	 * @atomic_destroy_state:
	 *
	 * Frees the private object state created with @atomic_duplicate_state.
	 */
	void (*atomic_destroy_state)(struct drm_private_obj *obj,
				     struct drm_private_state *state);

	/**
	 * @atomic_print_state:
	 *
	 * If driver subclasses &struct drm_private_state, it should implement
	 * this optional hook for printing additional driver specific state.
	 *
	 * Do not call this directly, use drm_atomic_private_obj_print_state()
	 * instead.
	 */
	void (*atomic_print_state)(struct drm_printer *p,
				   const struct drm_private_state *state);
};

/**
 * struct drm_private_obj - base struct for driver private atomic object
 *
 * A driver private object is initialized by calling
 * drm_atomic_private_obj_init() and cleaned up by calling
 * drm_atomic_private_obj_fini().
 *
 * Currently only tracks the state update functions and the opaque driver
 * private state itself, but in the future might also track which
 * &drm_modeset_lock is required to duplicate and update this object's state.
 *
 * All private objects must be initialized before the DRM device they are
 * attached to is registered to the DRM subsystem (call to drm_dev_register())
 * and should stay around until this DRM device is unregistered (call to
 * drm_dev_unregister()). In other words, private objects lifetime is tied
 * to the DRM device lifetime. This implies that:
 *
 * 1/ all calls to drm_atomic_private_obj_init() must be done before calling
 *    drm_dev_register()
 * 2/ all calls to drm_atomic_private_obj_fini() must be done after calling
 *    drm_dev_unregister()
 *
 * If that private object is used to store a state shared by multiple
 * CRTCs, proper care must be taken to ensure that non-blocking commits are
 * properly ordered to avoid a use-after-free issue.
 *
 * Indeed, assuming a sequence of two non-blocking &drm_atomic_commit on two
 * different &drm_crtc using different &drm_plane and &drm_connector, so with no
 * resources shared, there's no guarantee on which commit is going to happen
 * first. However, the second &drm_atomic_commit will consider the first
 * &drm_private_obj its old state, and will be in charge of freeing it whenever
 * the second &drm_atomic_commit is done.
 *
 * If the first &drm_atomic_commit happens after it, it will consider its
 * &drm_private_obj the new state and will be likely to access it, resulting in
 * an access to a freed memory region. Drivers should store (and get a reference
 * to) the &drm_crtc_commit structure in our private state in
 * &drm_mode_config_helper_funcs.atomic_commit_setup, and then wait for that
 * commit to complete as the first step of
 * &drm_mode_config_helper_funcs.atomic_commit_tail, similar to
 * drm_atomic_helper_wait_for_dependencies().
 */
struct drm_private_obj {
	/**
	 * @head: List entry used to attach a private object to a &drm_device
	 * (queued to &drm_mode_config.privobj_list).
	 */
	struct list_head head;

	/**
	 * @lock: Modeset lock to protect the state object.
	 */
	struct drm_modeset_lock lock;

	/**
	 * @state: Current atomic state for this driver private object.
	 */
	struct drm_private_state *state;

	/**
	 * @funcs:
	 *
	 * Functions to manipulate the state of this driver private object, see
	 * &drm_private_state_funcs.
	 */
	const struct drm_private_state_funcs *funcs;
};

/**
 * drm_for_each_privobj() - private object iterator
 *
 * @privobj: pointer to the current private object. Updated after each
 *	     iteration
 * @dev: the DRM device we want get private objects from
 *
 * Allows one to iterate over all private objects attached to @dev
 */
#define drm_for_each_privobj(privobj, dev) \
	list_for_each_entry(privobj, &(dev)->mode_config.privobj_list, head)

/**
 * struct drm_private_state - base struct for driver private object state
 *
 * Currently only contains a backpointer to the overall atomic update,
 * and the relevant private object but in the future also might hold
 * synchronization information similar to e.g. &drm_crtc.commit.
 */
struct drm_private_state {
	/**
	 * @state: backpointer to global drm_atomic_state
	 */
	struct drm_atomic_state *state;

	/**
	 * @obj: backpointer to the private object
	 */
	struct drm_private_obj *obj;
};

struct __drm_private_objs_state {
	struct drm_private_obj *ptr;
	struct drm_private_state *state, *old_state, *new_state;
};

/**
 * struct drm_atomic_state - Atomic commit structure
 *
 * This structure is the kernel counterpart of @drm_mode_atomic and represents
 * an atomic commit that transitions from an old to a new display state. It
 * contains all the objects affected by the atomic commit and both the new
 * state structures and pointers to the old state structures for
 * these.
 *
 * States are added to an atomic update by calling drm_atomic_get_crtc_state(),
 * drm_atomic_get_plane_state(), drm_atomic_get_connector_state(), or for
 * private state structures, drm_atomic_get_private_obj_state().
 */
struct drm_atomic_state {
	/**
	 * @ref:
	 *
	 * Count of all references to this update (will not be freed until zero).
	 */
	struct kref ref;

	/**
	 * @dev: Parent DRM Device.
	 */
	struct drm_device *dev;

	/**
	 * @allow_modeset:
	 *
	 * Allow full modeset. This is used by the ATOMIC IOCTL handler to
	 * implement the DRM_MODE_ATOMIC_ALLOW_MODESET flag. Drivers should
	 * generally not consult this flag, but instead look at the output of
	 * drm_atomic_crtc_needs_modeset(). The detailed rules are:
	 *
	 * - Drivers must not consult @allow_modeset in the atomic commit path.
	 *   Use drm_atomic_crtc_needs_modeset() instead.
	 *
	 * - Drivers must consult @allow_modeset before adding unrelated struct
	 *   drm_crtc_state to this commit by calling
	 *   drm_atomic_get_crtc_state(). See also the warning in the
	 *   documentation for that function.
	 *
	 * - Drivers must never change this flag, it is under the exclusive
	 *   control of userspace.
	 *
	 * - Drivers may consult @allow_modeset in the atomic check path, if
	 *   they have the choice between an optimal hardware configuration
	 *   which requires a modeset, and a less optimal configuration which
	 *   can be committed without a modeset. An example would be suboptimal
	 *   scanout FIFO allocation resulting in increased idle power
	 *   consumption. This allows userspace to avoid flickering and delays
	 *   for the normal composition loop at reasonable cost.
	 */
	bool allow_modeset : 1;
	/**
	 * @legacy_cursor_update:
	 *
	 * Hint to enforce legacy cursor IOCTL semantics.
	 *
	 * WARNING: This is thoroughly broken and pretty much impossible to
	 * implement correctly. Drivers must ignore this and should instead
	 * implement &drm_plane_helper_funcs.atomic_async_check and
	 * &drm_plane_helper_funcs.atomic_async_commit hooks. New users of this
	 * flag are not allowed.
	 */
	bool legacy_cursor_update : 1;

	/**
	 * @async_update: hint for asynchronous plane update
	 */
	bool async_update : 1;

	/**
	 * @duplicated:
	 *
	 * Indicates whether or not this atomic state was duplicated using
	 * drm_atomic_helper_duplicate_state(). Drivers and atomic helpers
	 * should use this to fixup normal  inconsistencies in duplicated
	 * states.
	 */
	bool duplicated : 1;

	/**
	 * @planes:
	 *
	 * Pointer to array of @drm_plane and @drm_plane_state part of this
	 * update.
	 */
	struct __drm_planes_state *planes;

	/**
	 * @crtcs:
	 *
	 * Pointer to array of @drm_crtc and @drm_crtc_state part of this
	 * update.
	 */
	struct __drm_crtcs_state *crtcs;

	/**
	 * @num_connector: size of the @connectors array
	 */
	int num_connector;

	/**
	 * @connectors:
	 *
	 * Pointer to array of @drm_connector and @drm_connector_state part of
	 * this update.
	 */
	struct __drm_connnectors_state *connectors;

	/**
	 * @num_private_objs: size of the @private_objs array
	 */
	int num_private_objs;

	/**
	 * @private_objs:
	 *
	 * Pointer to array of @drm_private_obj and @drm_private_obj_state part
	 * of this update.
	 */
	struct __drm_private_objs_state *private_objs;

	/**
	 * @acquire_ctx: acquire context for this atomic modeset state update
	 */
	struct drm_modeset_acquire_ctx *acquire_ctx;

	/**
	 * @fake_commit:
	 *
	 * Used for signaling unbound planes/connectors.
	 * When a connector or plane is not bound to any CRTC, it's still important
	 * to preserve linearity to prevent the atomic states from being freed too early.
	 *
	 * This commit (if set) is not bound to any CRTC, but will be completed when
	 * drm_atomic_helper_commit_hw_done() is called.
	 */
	struct drm_crtc_commit *fake_commit;

	/**
	 * @commit_work:
	 *
	 * Work item which can be used by the driver or helpers to execute the
	 * commit without blocking.
	 */
	struct work_struct commit_work;
};

void __drm_crtc_commit_free(struct kref *kref);

/**
 * drm_crtc_commit_get - acquire a reference to the CRTC commit
 * @commit: CRTC commit
 *
 * Increases the reference of @commit.
 *
 * Returns:
 * The pointer to @commit, with reference increased.
 */
static inline struct drm_crtc_commit *drm_crtc_commit_get(struct drm_crtc_commit *commit)
{
	kref_get(&commit->ref);
	return commit;
}

/**
 * drm_crtc_commit_put - release a reference to the CRTC commmit
 * @commit: CRTC commit
 *
 * This releases a reference to @commit which is freed after removing the
 * final reference. No locking required and callable from any context.
 */
static inline void drm_crtc_commit_put(struct drm_crtc_commit *commit)
{
	kref_put(&commit->ref, __drm_crtc_commit_free);
}

int drm_crtc_commit_wait(struct drm_crtc_commit *commit);

struct drm_atomic_state * __must_check
drm_atomic_state_alloc(struct drm_device *dev);
void drm_atomic_state_clear(struct drm_atomic_state *state);

/**
 * drm_atomic_state_get - acquire a reference to the atomic state
 * @state: The atomic state
 *
 * Returns a new reference to the @state
 */
static inline struct drm_atomic_state *
drm_atomic_state_get(struct drm_atomic_state *state)
{
	kref_get(&state->ref);
	return state;
}

void __drm_atomic_state_free(struct kref *ref);

/**
 * drm_atomic_state_put - release a reference to the atomic state
 * @state: The atomic state
 *
 * This releases a reference to @state which is freed after removing the
 * final reference. No locking required and callable from any context.
 */
static inline void drm_atomic_state_put(struct drm_atomic_state *state)
{
	kref_put(&state->ref, __drm_atomic_state_free);
}

int  __must_check
drm_atomic_state_init(struct drm_device *dev, struct drm_atomic_state *state);
void drm_atomic_state_default_clear(struct drm_atomic_state *state);
void drm_atomic_state_default_release(struct drm_atomic_state *state);

struct drm_crtc_state * __must_check
drm_atomic_get_crtc_state(struct drm_atomic_state *state,
			  struct drm_crtc *crtc);
struct drm_plane_state * __must_check
drm_atomic_get_plane_state(struct drm_atomic_state *state,
			   struct drm_plane *plane);
struct drm_connector_state * __must_check
drm_atomic_get_connector_state(struct drm_atomic_state *state,
			       struct drm_connector *connector);

void drm_atomic_private_obj_init(struct drm_device *dev,
				 struct drm_private_obj *obj,
				 struct drm_private_state *state,
				 const struct drm_private_state_funcs *funcs);
void drm_atomic_private_obj_fini(struct drm_private_obj *obj);

struct drm_private_state * __must_check
drm_atomic_get_private_obj_state(struct drm_atomic_state *state,
				 struct drm_private_obj *obj);
struct drm_private_state *
drm_atomic_get_old_private_obj_state(const struct drm_atomic_state *state,
				     struct drm_private_obj *obj);
struct drm_private_state *
drm_atomic_get_new_private_obj_state(const struct drm_atomic_state *state,
				     struct drm_private_obj *obj);

struct drm_connector *
drm_atomic_get_old_connector_for_encoder(const struct drm_atomic_state *state,
					 struct drm_encoder *encoder);
struct drm_connector *
drm_atomic_get_new_connector_for_encoder(const struct drm_atomic_state *state,
					 struct drm_encoder *encoder);

struct drm_crtc *
drm_atomic_get_old_crtc_for_encoder(struct drm_atomic_state *state,
					 struct drm_encoder *encoder);
struct drm_crtc *
drm_atomic_get_new_crtc_for_encoder(struct drm_atomic_state *state,
					 struct drm_encoder *encoder);

/**
 * drm_atomic_get_existing_crtc_state - get CRTC state, if it exists
 * @state: global atomic state object
 * @crtc: CRTC to grab
 *
 * This function returns the CRTC state for the given CRTC, or NULL
 * if the CRTC is not part of the global atomic state.
 *
 * This function is deprecated, @drm_atomic_get_old_crtc_state or
 * @drm_atomic_get_new_crtc_state should be used instead.
 */
static inline struct drm_crtc_state *
drm_atomic_get_existing_crtc_state(const struct drm_atomic_state *state,
				   struct drm_crtc *crtc)
{
	return state->crtcs[drm_crtc_index(crtc)].state;
}

/**
 * drm_atomic_get_old_crtc_state - get old CRTC state, if it exists
 * @state: global atomic state object
 * @crtc: CRTC to grab
 *
 * This function returns the old CRTC state for the given CRTC, or
 * NULL if the CRTC is not part of the global atomic state.
 */
static inline struct drm_crtc_state *
drm_atomic_get_old_crtc_state(const struct drm_atomic_state *state,
			      struct drm_crtc *crtc)
{
	return state->crtcs[drm_crtc_index(crtc)].old_state;
}
/**
 * drm_atomic_get_new_crtc_state - get new CRTC state, if it exists
 * @state: global atomic state object
 * @crtc: CRTC to grab
 *
 * This function returns the new CRTC state for the given CRTC, or
 * NULL if the CRTC is not part of the global atomic state.
 */
static inline struct drm_crtc_state *
drm_atomic_get_new_crtc_state(const struct drm_atomic_state *state,
			      struct drm_crtc *crtc)
{
	return state->crtcs[drm_crtc_index(crtc)].new_state;
}

/**
 * drm_atomic_get_existing_plane_state - get plane state, if it exists
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the plane state for the given plane, or NULL
 * if the plane is not part of the global atomic state.
 *
 * This function is deprecated, @drm_atomic_get_old_plane_state or
 * @drm_atomic_get_new_plane_state should be used instead.
 */
static inline struct drm_plane_state *
drm_atomic_get_existing_plane_state(const struct drm_atomic_state *state,
				    struct drm_plane *plane)
{
	return state->planes[drm_plane_index(plane)].state;
}

/**
 * drm_atomic_get_old_plane_state - get plane state, if it exists
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the old plane state for the given plane, or
 * NULL if the plane is not part of the global atomic state.
 */
static inline struct drm_plane_state *
drm_atomic_get_old_plane_state(const struct drm_atomic_state *state,
			       struct drm_plane *plane)
{
	return state->planes[drm_plane_index(plane)].old_state;
}

/**
 * drm_atomic_get_new_plane_state - get plane state, if it exists
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the new plane state for the given plane, or
 * NULL if the plane is not part of the global atomic state.
 */
static inline struct drm_plane_state *
drm_atomic_get_new_plane_state(const struct drm_atomic_state *state,
			       struct drm_plane *plane)
{
	return state->planes[drm_plane_index(plane)].new_state;
}

/**
 * drm_atomic_get_existing_connector_state - get connector state, if it exists
 * @state: global atomic state object
 * @connector: connector to grab
 *
 * This function returns the connector state for the given connector,
 * or NULL if the connector is not part of the global atomic state.
 *
 * This function is deprecated, @drm_atomic_get_old_connector_state or
 * @drm_atomic_get_new_connector_state should be used instead.
 */
static inline struct drm_connector_state *
drm_atomic_get_existing_connector_state(const struct drm_atomic_state *state,
					struct drm_connector *connector)
{
	int index = drm_connector_index(connector);

	if (index >= state->num_connector)
		return NULL;

	return state->connectors[index].state;
}

/**
 * drm_atomic_get_old_connector_state - get connector state, if it exists
 * @state: global atomic state object
 * @connector: connector to grab
 *
 * This function returns the old connector state for the given connector,
 * or NULL if the connector is not part of the global atomic state.
 */
static inline struct drm_connector_state *
drm_atomic_get_old_connector_state(const struct drm_atomic_state *state,
				   struct drm_connector *connector)
{
	int index = drm_connector_index(connector);

	if (index >= state->num_connector)
		return NULL;

	return state->connectors[index].old_state;
}

/**
 * drm_atomic_get_new_connector_state - get connector state, if it exists
 * @state: global atomic state object
 * @connector: connector to grab
 *
 * This function returns the new connector state for the given connector,
 * or NULL if the connector is not part of the global atomic state.
 */
static inline struct drm_connector_state *
drm_atomic_get_new_connector_state(const struct drm_atomic_state *state,
				   struct drm_connector *connector)
{
	int index = drm_connector_index(connector);

	if (index >= state->num_connector)
		return NULL;

	return state->connectors[index].new_state;
}

/**
 * __drm_atomic_get_current_plane_state - get current plane state
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the plane state for the given plane, either from
 * @state, or if the plane isn't part of the atomic state update, from @plane.
 * This is useful in atomic check callbacks, when drivers need to peek at, but
 * not change, state of other planes, since it avoids threading an error code
 * back up the call chain.
 *
 * WARNING:
 *
 * Note that this function is in general unsafe since it doesn't check for the
 * required locking for access state structures. Drivers must ensure that it is
 * safe to access the returned state structure through other means. One common
 * example is when planes are fixed to a single CRTC, and the driver knows that
 * the CRTC lock is held already. In that case holding the CRTC lock gives a
 * read-lock on all planes connected to that CRTC. But if planes can be
 * reassigned things get more tricky. In that case it's better to use
 * drm_atomic_get_plane_state and wire up full error handling.
 *
 * Returns:
 *
 * Read-only pointer to the current plane state.
 */
static inline const struct drm_plane_state *
__drm_atomic_get_current_plane_state(const struct drm_atomic_state *state,
				     struct drm_plane *plane)
{
	if (state->planes[drm_plane_index(plane)].state)
		return state->planes[drm_plane_index(plane)].state;

	return plane->state;
}

int __must_check
drm_atomic_add_encoder_bridges(struct drm_atomic_state *state,
			       struct drm_encoder *encoder);
int __must_check
drm_atomic_add_affected_connectors(struct drm_atomic_state *state,
				   struct drm_crtc *crtc);
int __must_check
drm_atomic_add_affected_planes(struct drm_atomic_state *state,
			       struct drm_crtc *crtc);

int __must_check drm_atomic_check_only(struct drm_atomic_state *state);
int __must_check drm_atomic_commit(struct drm_atomic_state *state);
int __must_check drm_atomic_nonblocking_commit(struct drm_atomic_state *state);

void drm_state_dump(struct drm_device *dev, struct drm_printer *p);

/**
 * for_each_oldnew_connector_in_state - iterate over all connectors in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @connector: &struct drm_connector iteration cursor
 * @old_connector_state: &struct drm_connector_state iteration cursor for the
 * 	old state
 * @new_connector_state: &struct drm_connector_state iteration cursor for the
 * 	new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all connectors in an atomic update, tracking both old and
 * new state. This is useful in places where the state delta needs to be
 * considered, for example in atomic check functions.
 */
#define for_each_oldnew_connector_in_state(__state, connector, old_connector_state, new_connector_state, __i) \
	for ((__i) = 0;								\
	     (__i) < (__state)->num_connector;					\
	     (__i)++)								\
		for_each_if ((__state)->connectors[__i].ptr &&			\
			     ((connector) = (__state)->connectors[__i].ptr,	\
			     (void)(connector) /* Only to avoid unused-but-set-variable warning */, \
			     (old_connector_state) = (__state)->connectors[__i].old_state,	\
			     (new_connector_state) = (__state)->connectors[__i].new_state, 1))

/**
 * for_each_old_connector_in_state - iterate over all connectors in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @connector: &struct drm_connector iteration cursor
 * @old_connector_state: &struct drm_connector_state iteration cursor for the
 * 	old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all connectors in an atomic update, tracking only the old
 * state. This is useful in disable functions, where we need the old state the
 * hardware is still in.
 */
#define for_each_old_connector_in_state(__state, connector, old_connector_state, __i) \
	for ((__i) = 0;								\
	     (__i) < (__state)->num_connector;					\
	     (__i)++)								\
		for_each_if ((__state)->connectors[__i].ptr &&			\
			     ((connector) = (__state)->connectors[__i].ptr,	\
			     (void)(connector) /* Only to avoid unused-but-set-variable warning */, \
			     (old_connector_state) = (__state)->connectors[__i].old_state, 1))

/**
 * for_each_new_connector_in_state - iterate over all connectors in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @connector: &struct drm_connector iteration cursor
 * @new_connector_state: &struct drm_connector_state iteration cursor for the
 * 	new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all connectors in an atomic update, tracking only the new
 * state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_connector_in_state(__state, connector, new_connector_state, __i) \
	for ((__i) = 0;								\
	     (__i) < (__state)->num_connector;					\
	     (__i)++)								\
		for_each_if ((__state)->connectors[__i].ptr &&			\
			     ((connector) = (__state)->connectors[__i].ptr,	\
			     (void)(connector) /* Only to avoid unused-but-set-variable warning */, \
			     (new_connector_state) = (__state)->connectors[__i].new_state, \
			     (void)(new_connector_state) /* Only to avoid unused-but-set-variable warning */, 1))

/**
 * for_each_oldnew_crtc_in_state - iterate over all CRTCs in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @crtc: &struct drm_crtc iteration cursor
 * @old_crtc_state: &struct drm_crtc_state iteration cursor for the old state
 * @new_crtc_state: &struct drm_crtc_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all CRTCs in an atomic update, tracking both old and
 * new state. This is useful in places where the state delta needs to be
 * considered, for example in atomic check functions.
 */
#define for_each_oldnew_crtc_in_state(__state, crtc, old_crtc_state, new_crtc_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_crtc;		\
	     (__i)++)							\
		for_each_if ((__state)->crtcs[__i].ptr &&		\
			     ((crtc) = (__state)->crtcs[__i].ptr,	\
			      (void)(crtc) /* Only to avoid unused-but-set-variable warning */, \
			     (old_crtc_state) = (__state)->crtcs[__i].old_state, \
			     (void)(old_crtc_state) /* Only to avoid unused-but-set-variable warning */, \
			     (new_crtc_state) = (__state)->crtcs[__i].new_state, \
			     (void)(new_crtc_state) /* Only to avoid unused-but-set-variable warning */, 1))

/**
 * for_each_old_crtc_in_state - iterate over all CRTCs in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @crtc: &struct drm_crtc iteration cursor
 * @old_crtc_state: &struct drm_crtc_state iteration cursor for the old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all CRTCs in an atomic update, tracking only the old
 * state. This is useful in disable functions, where we need the old state the
 * hardware is still in.
 */
#define for_each_old_crtc_in_state(__state, crtc, old_crtc_state, __i)	\
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_crtc;		\
	     (__i)++)							\
		for_each_if ((__state)->crtcs[__i].ptr &&		\
			     ((crtc) = (__state)->crtcs[__i].ptr,	\
			     (void)(crtc) /* Only to avoid unused-but-set-variable warning */, \
			     (old_crtc_state) = (__state)->crtcs[__i].old_state, 1))

/**
 * for_each_new_crtc_in_state - iterate over all CRTCs in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @crtc: &struct drm_crtc iteration cursor
 * @new_crtc_state: &struct drm_crtc_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all CRTCs in an atomic update, tracking only the new
 * state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_crtc_in_state(__state, crtc, new_crtc_state, __i)	\
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_crtc;		\
	     (__i)++)							\
		for_each_if ((__state)->crtcs[__i].ptr &&		\
			     ((crtc) = (__state)->crtcs[__i].ptr,	\
			     (void)(crtc) /* Only to avoid unused-but-set-variable warning */, \
			     (new_crtc_state) = (__state)->crtcs[__i].new_state, \
			     (void)(new_crtc_state) /* Only to avoid unused-but-set-variable warning */, 1))

/**
 * for_each_oldnew_plane_in_state - iterate over all planes in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @old_plane_state: &struct drm_plane_state iteration cursor for the old state
 * @new_plane_state: &struct drm_plane_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update, tracking both old and
 * new state. This is useful in places where the state delta needs to be
 * considered, for example in atomic check functions.
 */
#define for_each_oldnew_plane_in_state(__state, plane, old_plane_state, new_plane_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_total_plane;	\
	     (__i)++)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (void)(plane) /* Only to avoid unused-but-set-variable warning */, \
			      (old_plane_state) = (__state)->planes[__i].old_state,\
			      (new_plane_state) = (__state)->planes[__i].new_state, 1))

/**
 * for_each_oldnew_plane_in_state_reverse - iterate over all planes in an atomic
 * update in reverse order
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @old_plane_state: &struct drm_plane_state iteration cursor for the old state
 * @new_plane_state: &struct drm_plane_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update in reverse order,
 * tracking both old and  new state. This is useful in places where the
 * state delta needs to be considered, for example in atomic check functions.
 */
#define for_each_oldnew_plane_in_state_reverse(__state, plane, old_plane_state, new_plane_state, __i) \
	for ((__i) = ((__state)->dev->mode_config.num_total_plane - 1);	\
	     (__i) >= 0;						\
	     (__i)--)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (old_plane_state) = (__state)->planes[__i].old_state,\
			      (new_plane_state) = (__state)->planes[__i].new_state, 1))

/**
 * for_each_new_plane_in_state_reverse - other than only tracking new state,
 * it's the same as for_each_oldnew_plane_in_state_reverse
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @new_plane_state: &struct drm_plane_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 */
#define for_each_new_plane_in_state_reverse(__state, plane, new_plane_state, __i) \
	for ((__i) = ((__state)->dev->mode_config.num_total_plane - 1);	\
	     (__i) >= 0;						\
	     (__i)--)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (new_plane_state) = (__state)->planes[__i].new_state, 1))

/**
 * for_each_old_plane_in_state - iterate over all planes in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @old_plane_state: &struct drm_plane_state iteration cursor for the old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update, tracking only the old
 * state. This is useful in disable functions, where we need the old state the
 * hardware is still in.
 */
#define for_each_old_plane_in_state(__state, plane, old_plane_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_total_plane;	\
	     (__i)++)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (old_plane_state) = (__state)->planes[__i].old_state, 1))
/**
 * for_each_new_plane_in_state - iterate over all planes in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @new_plane_state: &struct drm_plane_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update, tracking only the new
 * state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_plane_in_state(__state, plane, new_plane_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_total_plane;	\
	     (__i)++)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (void)(plane) /* Only to avoid unused-but-set-variable warning */, \
			      (new_plane_state) = (__state)->planes[__i].new_state, \
			      (void)(new_plane_state) /* Only to avoid unused-but-set-variable warning */, 1))

/**
 * for_each_oldnew_private_obj_in_state - iterate over all private objects in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @obj: &struct drm_private_obj iteration cursor
 * @old_obj_state: &struct drm_private_state iteration cursor for the old state
 * @new_obj_state: &struct drm_private_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all private objects in an atomic update, tracking both
 * old and new state. This is useful in places where the state delta needs
 * to be considered, for example in atomic check functions.
 */
#define for_each_oldnew_private_obj_in_state(__state, obj, old_obj_state, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs && \
		     ((obj) = (__state)->private_objs[__i].ptr, \
		      (old_obj_state) = (__state)->private_objs[__i].old_state,	\
		      (new_obj_state) = (__state)->private_objs[__i].new_state, 1); \
	     (__i)++)

/**
 * for_each_old_private_obj_in_state - iterate over all private objects in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @obj: &struct drm_private_obj iteration cursor
 * @old_obj_state: &struct drm_private_state iteration cursor for the old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all private objects in an atomic update, tracking only
 * the old state. This is useful in disable functions, where we need the old
 * state the hardware is still in.
 */
#define for_each_old_private_obj_in_state(__state, obj, old_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs && \
		     ((obj) = (__state)->private_objs[__i].ptr, \
		      (old_obj_state) = (__state)->private_objs[__i].old_state, 1); \
	     (__i)++)

/**
 * for_each_new_private_obj_in_state - iterate over all private objects in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @obj: &struct drm_private_obj iteration cursor
 * @new_obj_state: &struct drm_private_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all private objects in an atomic update, tracking only
 * the new state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_private_obj_in_state(__state, obj, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs && \
		     ((obj) = (__state)->private_objs[__i].ptr, \
		      (void)(obj) /* Only to avoid unused-but-set-variable warning */, \
		      (new_obj_state) = (__state)->private_objs[__i].new_state, 1); \
	     (__i)++)

/**
 * drm_atomic_crtc_needs_modeset - compute combined modeset need
 * @state: &drm_crtc_state for the CRTC
 *
 * To give drivers flexibility &struct drm_crtc_state has 3 booleans to track
 * whether the state CRTC changed enough to need a full modeset cycle:
 * mode_changed, active_changed and connectors_changed. This helper simply
 * combines these three to compute the overall need for a modeset for @state.
 *
 * The atomic helper code sets these booleans, but drivers can and should
 * change them appropriately to accurately represent whether a modeset is
 * really needed. In general, drivers should avoid full modesets whenever
 * possible.
 *
 * For example if the CRTC mode has changed, and the hardware is able to enact
 * the requested mode change without going through a full modeset, the driver
 * should clear mode_changed in its &drm_mode_config_funcs.atomic_check
 * implementation.
 */
static inline bool
drm_atomic_crtc_needs_modeset(const struct drm_crtc_state *state)
{
	return state->mode_changed || state->active_changed ||
	       state->connectors_changed;
}

/**
 * drm_atomic_crtc_effectively_active - compute whether CRTC is actually active
 * @state: &drm_crtc_state for the CRTC
 *
 * When in self refresh mode, the crtc_state->active value will be false, since
 * the CRTC is off. However in some cases we're interested in whether the CRTC
 * is active, or effectively active (ie: it's connected to an active display).
 * In these cases, use this function instead of just checking active.
 */
static inline bool
drm_atomic_crtc_effectively_active(const struct drm_crtc_state *state)
{
	return state->active || state->self_refresh_active;
}

/**
 * struct drm_bus_cfg - bus configuration
 *
 * This structure stores the configuration of a physical bus between two
 * components in an output pipeline, usually between two bridges, an encoder
 * and a bridge, or a bridge and a connector.
 *
 * The bus configuration is stored in &drm_bridge_state separately for the
 * input and output buses, as seen from the point of view of each bridge. The
 * bus configuration of a bridge output is usually identical to the
 * configuration of the next bridge's input, but may differ if the signals are
 * modified between the two bridges, for instance by an inverter on the board.
 * The input and output configurations of a bridge may differ if the bridge
 * modifies the signals internally, for instance by performing format
 * conversion, or modifying signals polarities.
 */
struct drm_bus_cfg {
	/**
	 * @format: format used on this bus (one of the MEDIA_BUS_FMT_* format)
	 *
	 * This field should not be directly modified by drivers
	 * (drm_atomic_bridge_chain_select_bus_fmts() takes care of the bus
	 * format negotiation).
	 */
	u32 format;

	/**
	 * @flags: DRM_BUS_* flags used on this bus
	 */
	u32 flags;
};

/**
 * struct drm_bridge_state - Atomic bridge state object
 */
struct drm_bridge_state {
	/**
	 * @base: inherit from &drm_private_state
	 */
	struct drm_private_state base;

	/**
	 * @bridge: the bridge this state refers to
	 */
	struct drm_bridge *bridge;

	/**
	 * @input_bus_cfg: input bus configuration
	 */
	struct drm_bus_cfg input_bus_cfg;

	/**
	 * @output_bus_cfg: output bus configuration
	 */
	struct drm_bus_cfg output_bus_cfg;
};

static inline struct drm_bridge_state *
drm_priv_to_bridge_state(struct drm_private_state *priv)
{
	return container_of(priv, struct drm_bridge_state, base);
}

struct drm_bridge_state *
drm_atomic_get_bridge_state(struct drm_atomic_state *state,
			    struct drm_bridge *bridge);
struct drm_bridge_state *
drm_atomic_get_old_bridge_state(const struct drm_atomic_state *state,
				struct drm_bridge *bridge);
struct drm_bridge_state *
drm_atomic_get_new_bridge_state(const struct drm_atomic_state *state,
				struct drm_bridge *bridge);

#endif /* DRM_ATOMIC_H_ */
