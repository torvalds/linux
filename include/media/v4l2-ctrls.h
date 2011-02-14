/*
    V4L2 controls support header.

    Copyright (C) 2010  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _V4L2_CTRLS_H
#define _V4L2_CTRLS_H

#include <linux/list.h>
#include <linux/device.h>
#include <linux/videodev2.h>

/* forward references */
struct v4l2_ctrl_handler;
struct v4l2_ctrl;
struct video_device;
struct v4l2_subdev;

/** struct v4l2_ctrl_ops - The control operations that the driver has to provide.
  * @g_volatile_ctrl: Get a new value for this control. Generally only relevant
  *		for volatile (and usually read-only) controls such as a control
  *		that returns the current signal strength which changes
  *		continuously.
  *		If not set, then the currently cached value will be returned.
  * @try_ctrl:	Test whether the control's value is valid. Only relevant when
  *		the usual min/max/step checks are not sufficient.
  * @s_ctrl:	Actually set the new control value. s_ctrl is compulsory. The
  *		ctrl->handler->lock is held when these ops are called, so no
  *		one else can access controls owned by that handler.
  */
struct v4l2_ctrl_ops {
	int (*g_volatile_ctrl)(struct v4l2_ctrl *ctrl);
	int (*try_ctrl)(struct v4l2_ctrl *ctrl);
	int (*s_ctrl)(struct v4l2_ctrl *ctrl);
};

/** struct v4l2_ctrl - The control structure.
  * @node:	The list node.
  * @handler:	The handler that owns the control.
  * @cluster:	Point to start of cluster array.
  * @ncontrols:	Number of controls in cluster array.
  * @done:	Internal flag: set for each processed control.
  * @is_new:	Set when the user specified a new value for this control. It
  *		is also set when called from v4l2_ctrl_handler_setup. Drivers
  *		should never set this flag.
  * @is_private: If set, then this control is private to its handler and it
  *		will not be added to any other handlers. Drivers can set
  *		this flag.
  * @is_volatile: If set, then this control is volatile. This means that the
  *		control's current value cannot be cached and needs to be
  *		retrieved through the g_volatile_ctrl op. Drivers can set
  *		this flag.
  * @ops:	The control ops.
  * @id:	The control ID.
  * @name:	The control name.
  * @type:	The control type.
  * @minimum:	The control's minimum value.
  * @maximum:	The control's maximum value.
  * @default_value: The control's default value.
  * @step:	The control's step value for non-menu controls.
  * @menu_skip_mask: The control's skip mask for menu controls. This makes it
  *		easy to skip menu items that are not valid. If bit X is set,
  *		then menu item X is skipped. Of course, this only works for
  *		menus with <= 32 menu items. There are no menus that come
  *		close to that number, so this is OK. Should we ever need more,
  *		then this will have to be extended to a u64 or a bit array.
  * @qmenu:	A const char * array for all menu items. Array entries that are
  *		empty strings ("") correspond to non-existing menu items (this
  *		is in addition to the menu_skip_mask above). The last entry
  *		must be NULL.
  * @flags:	The control's flags.
  * @cur:	The control's current value.
  * @val:	The control's new s32 value.
  * @val64:	The control's new s64 value.
  * @string:	The control's new string value.
  * @priv:	The control's private pointer. For use by the driver. It is
  *		untouched by the control framework. Note that this pointer is
  *		not freed when the control is deleted. Should this be needed
  *		then a new internal bitfield can be added to tell the framework
  *		to free this pointer.
  */
struct v4l2_ctrl {
	/* Administrative fields */
	struct list_head node;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl **cluster;
	unsigned ncontrols;
	unsigned int done:1;

	unsigned int is_new:1;
	unsigned int is_private:1;
	unsigned int is_volatile:1;

	const struct v4l2_ctrl_ops *ops;
	u32 id;
	const char *name;
	enum v4l2_ctrl_type type;
	s32 minimum, maximum, default_value;
	union {
		u32 step;
		u32 menu_skip_mask;
	};
	const char * const *qmenu;
	unsigned long flags;
	union {
		s32 val;
		s64 val64;
		char *string;
	} cur;
	union {
		s32 val;
		s64 val64;
		char *string;
	};
	void *priv;
};

/** struct v4l2_ctrl_ref - The control reference.
  * @node:	List node for the sorted list.
  * @next:	Single-link list node for the hash.
  * @ctrl:	The actual control information.
  *
  * Each control handler has a list of these refs. The list_head is used to
  * keep a sorted-by-control-ID list of all controls, while the next pointer
  * is used to link the control in the hash's bucket.
  */
struct v4l2_ctrl_ref {
	struct list_head node;
	struct v4l2_ctrl_ref *next;
	struct v4l2_ctrl *ctrl;
};

/** struct v4l2_ctrl_handler - The control handler keeps track of all the
  * controls: both the controls owned by the handler and those inherited
  * from other handlers.
  * @lock:	Lock to control access to this handler and its controls.
  * @ctrls:	The list of controls owned by this handler.
  * @ctrl_refs:	The list of control references.
  * @cached:	The last found control reference. It is common that the same
  *		control is needed multiple times, so this is a simple
  *		optimization.
  * @buckets:	Buckets for the hashing. Allows for quick control lookup.
  * @nr_of_buckets: Total number of buckets in the array.
  * @error:	The error code of the first failed control addition.
  */
struct v4l2_ctrl_handler {
	struct mutex lock;
	struct list_head ctrls;
	struct list_head ctrl_refs;
	struct v4l2_ctrl_ref *cached;
	struct v4l2_ctrl_ref **buckets;
	u16 nr_of_buckets;
	int error;
};

/** struct v4l2_ctrl_config - Control configuration structure.
  * @ops:	The control ops.
  * @id:	The control ID.
  * @name:	The control name.
  * @type:	The control type.
  * @min:	The control's minimum value.
  * @max:	The control's maximum value.
  * @step:	The control's step value for non-menu controls.
  * @def: 	The control's default value.
  * @flags:	The control's flags.
  * @menu_skip_mask: The control's skip mask for menu controls. This makes it
  *		easy to skip menu items that are not valid. If bit X is set,
  *		then menu item X is skipped. Of course, this only works for
  *		menus with <= 32 menu items. There are no menus that come
  *		close to that number, so this is OK. Should we ever need more,
  *		then this will have to be extended to a u64 or a bit array.
  * @qmenu:	A const char * array for all menu items. Array entries that are
  *		empty strings ("") correspond to non-existing menu items (this
  *		is in addition to the menu_skip_mask above). The last entry
  *		must be NULL.
  * @is_private: If set, then this control is private to its handler and it
  *		will not be added to any other handlers.
  * @is_volatile: If set, then this control is volatile. This means that the
  *		control's current value cannot be cached and needs to be
  *		retrieved through the g_volatile_ctrl op.
  */
struct v4l2_ctrl_config {
	const struct v4l2_ctrl_ops *ops;
	u32 id;
	const char *name;
	enum v4l2_ctrl_type type;
	s32 min;
	s32 max;
	u32 step;
	s32 def;
	u32 flags;
	u32 menu_skip_mask;
	const char * const *qmenu;
	unsigned int is_private:1;
	unsigned int is_volatile:1;
};

/** v4l2_ctrl_fill() - Fill in the control fields based on the control ID.
  *
  * This works for all standard V4L2 controls.
  * For non-standard controls it will only fill in the given arguments
  * and @name will be NULL.
  *
  * This function will overwrite the contents of @name, @type and @flags.
  * The contents of @min, @max, @step and @def may be modified depending on
  * the type.
  *
  * Do not use in drivers! It is used internally for backwards compatibility
  * control handling only. Once all drivers are converted to use the new
  * control framework this function will no longer be exported.
  */
void v4l2_ctrl_fill(u32 id, const char **name, enum v4l2_ctrl_type *type,
		    s32 *min, s32 *max, s32 *step, s32 *def, u32 *flags);


/** v4l2_ctrl_handler_init() - Initialize the control handler.
  * @hdl:	The control handler.
  * @nr_of_controls_hint: A hint of how many controls this handler is
  *		expected to refer to. This is the total number, so including
  *		any inherited controls. It doesn't have to be precise, but if
  *		it is way off, then you either waste memory (too many buckets
  *		are allocated) or the control lookup becomes slower (not enough
  *		buckets are allocated, so there are more slow list lookups).
  *		It will always work, though.
  *
  * Returns an error if the buckets could not be allocated. This error will
  * also be stored in @hdl->error.
  */
int v4l2_ctrl_handler_init(struct v4l2_ctrl_handler *hdl,
			   unsigned nr_of_controls_hint);

/** v4l2_ctrl_handler_free() - Free all controls owned by the handler and free
  * the control list.
  * @hdl:	The control handler.
  *
  * Does nothing if @hdl == NULL.
  */
void v4l2_ctrl_handler_free(struct v4l2_ctrl_handler *hdl);

/** v4l2_ctrl_handler_setup() - Call the s_ctrl op for all controls belonging
  * to the handler to initialize the hardware to the current control values.
  * @hdl:	The control handler.
  *
  * Button controls will be skipped, as are read-only controls.
  *
  * If @hdl == NULL, then this just returns 0.
  */
int v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *hdl);

/** v4l2_ctrl_handler_log_status() - Log all controls owned by the handler.
  * @hdl:	The control handler.
  * @prefix:	The prefix to use when logging the control values. If the
  *		prefix does not end with a space, then ": " will be added
  *		after the prefix. If @prefix == NULL, then no prefix will be
  *		used.
  *
  * For use with VIDIOC_LOG_STATUS.
  *
  * Does nothing if @hdl == NULL.
  */
void v4l2_ctrl_handler_log_status(struct v4l2_ctrl_handler *hdl,
				  const char *prefix);

/** v4l2_ctrl_new_custom() - Allocate and initialize a new custom V4L2
  * control.
  * @hdl:	The control handler.
  * @cfg:	The control's configuration data.
  * @priv:	The control's driver-specific private data.
  *
  * If the &v4l2_ctrl struct could not be allocated then NULL is returned
  * and @hdl->error is set to the error code (if it wasn't set already).
  */
struct v4l2_ctrl *v4l2_ctrl_new_custom(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_config *cfg, void *priv);

/** v4l2_ctrl_new_std() - Allocate and initialize a new standard V4L2 non-menu control.
  * @hdl:	The control handler.
  * @ops:	The control ops.
  * @id:	The control ID.
  * @min:	The control's minimum value.
  * @max:	The control's maximum value.
  * @step:	The control's step value
  * @def: 	The control's default value.
  *
  * If the &v4l2_ctrl struct could not be allocated, or the control
  * ID is not known, then NULL is returned and @hdl->error is set to the
  * appropriate error code (if it wasn't set already).
  *
  * If @id refers to a menu control, then this function will return NULL.
  *
  * Use v4l2_ctrl_new_std_menu() when adding menu controls.
  */
struct v4l2_ctrl *v4l2_ctrl_new_std(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s32 min, s32 max, u32 step, s32 def);

/** v4l2_ctrl_new_std_menu() - Allocate and initialize a new standard V4L2 menu control.
  * @hdl:	The control handler.
  * @ops:	The control ops.
  * @id:	The control ID.
  * @max:	The control's maximum value.
  * @mask: 	The control's skip mask for menu controls. This makes it
  *		easy to skip menu items that are not valid. If bit X is set,
  *		then menu item X is skipped. Of course, this only works for
  *		menus with <= 32 menu items. There are no menus that come
  *		close to that number, so this is OK. Should we ever need more,
  *		then this will have to be extended to a u64 or a bit array.
  * @def: 	The control's default value.
  *
  * Same as v4l2_ctrl_new_std(), but @min is set to 0 and the @mask value
  * determines which menu items are to be skipped.
  *
  * If @id refers to a non-menu control, then this function will return NULL.
  */
struct v4l2_ctrl *v4l2_ctrl_new_std_menu(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s32 max, s32 mask, s32 def);

/** v4l2_ctrl_add_ctrl() - Add a control from another handler to this handler.
  * @hdl:	The control handler.
  * @ctrl:	The control to add.
  *
  * It will return NULL if it was unable to add the control reference.
  * If the control already belonged to the handler, then it will do
  * nothing and just return @ctrl.
  */
struct v4l2_ctrl *v4l2_ctrl_add_ctrl(struct v4l2_ctrl_handler *hdl,
					  struct v4l2_ctrl *ctrl);

/** v4l2_ctrl_add_handler() - Add all controls from handler @add to
  * handler @hdl.
  * @hdl:	The control handler.
  * @add:	The control handler whose controls you want to add to
  *		the @hdl control handler.
  *
  * Does nothing if either of the two is a NULL pointer.
  * In case of an error @hdl->error will be set to the error code (if it
  * wasn't set already).
  */
int v4l2_ctrl_add_handler(struct v4l2_ctrl_handler *hdl,
			  struct v4l2_ctrl_handler *add);


/** v4l2_ctrl_cluster() - Mark all controls in the cluster as belonging to that cluster.
  * @ncontrols:	The number of controls in this cluster.
  * @controls: 	The cluster control array of size @ncontrols.
  */
void v4l2_ctrl_cluster(unsigned ncontrols, struct v4l2_ctrl **controls);


/** v4l2_ctrl_find() - Find a control with the given ID.
  * @hdl:	The control handler.
  * @id:	The control ID to find.
  *
  * If @hdl == NULL this will return NULL as well. Will lock the handler so
  * do not use from inside &v4l2_ctrl_ops.
  */
struct v4l2_ctrl *v4l2_ctrl_find(struct v4l2_ctrl_handler *hdl, u32 id);

/** v4l2_ctrl_activate() - Make the control active or inactive.
  * @ctrl:	The control to (de)activate.
  * @active:	True if the control should become active.
  *
  * This sets or clears the V4L2_CTRL_FLAG_INACTIVE flag atomically.
  * Does nothing if @ctrl == NULL.
  * This will usually be called from within the s_ctrl op.
  *
  * This function can be called regardless of whether the control handler
  * is locked or not.
  */
void v4l2_ctrl_activate(struct v4l2_ctrl *ctrl, bool active);

/** v4l2_ctrl_grab() - Mark the control as grabbed or not grabbed.
  * @ctrl:	The control to (de)activate.
  * @grabbed:	True if the control should become grabbed.
  *
  * This sets or clears the V4L2_CTRL_FLAG_GRABBED flag atomically.
  * Does nothing if @ctrl == NULL.
  * This will usually be called when starting or stopping streaming in the
  * driver.
  *
  * This function can be called regardless of whether the control handler
  * is locked or not.
  */
void v4l2_ctrl_grab(struct v4l2_ctrl *ctrl, bool grabbed);

/** v4l2_ctrl_lock() - Helper function to lock the handler
  * associated with the control.
  * @ctrl:	The control to lock.
  */
static inline void v4l2_ctrl_lock(struct v4l2_ctrl *ctrl)
{
	mutex_lock(&ctrl->handler->lock);
}

/** v4l2_ctrl_lock() - Helper function to unlock the handler
  * associated with the control.
  * @ctrl:	The control to unlock.
  */
static inline void v4l2_ctrl_unlock(struct v4l2_ctrl *ctrl)
{
	mutex_unlock(&ctrl->handler->lock);
}

/** v4l2_ctrl_g_ctrl() - Helper function to get the control's value from within a driver.
  * @ctrl:	The control.
  *
  * This returns the control's value safely by going through the control
  * framework. This function will lock the control's handler, so it cannot be
  * used from within the &v4l2_ctrl_ops functions.
  *
  * This function is for integer type controls only.
  */
s32 v4l2_ctrl_g_ctrl(struct v4l2_ctrl *ctrl);

/** v4l2_ctrl_s_ctrl() - Helper function to set the control's value from within a driver.
  * @ctrl:	The control.
  * @val:	The new value.
  *
  * This set the control's new value safely by going through the control
  * framework. This function will lock the control's handler, so it cannot be
  * used from within the &v4l2_ctrl_ops functions.
  *
  * This function is for integer type controls only.
  */
int v4l2_ctrl_s_ctrl(struct v4l2_ctrl *ctrl, s32 val);


/* Helpers for ioctl_ops. If hdl == NULL then they will all return -EINVAL. */
int v4l2_queryctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_queryctrl *qc);
int v4l2_querymenu(struct v4l2_ctrl_handler *hdl, struct v4l2_querymenu *qm);
int v4l2_g_ctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_control *ctrl);
int v4l2_s_ctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_control *ctrl);
int v4l2_g_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct v4l2_ext_controls *c);
int v4l2_try_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct v4l2_ext_controls *c);
int v4l2_s_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct v4l2_ext_controls *c);

/* Helpers for subdevices. If the associated ctrl_handler == NULL then they
   will all return -EINVAL. */
int v4l2_subdev_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc);
int v4l2_subdev_querymenu(struct v4l2_subdev *sd, struct v4l2_querymenu *qm);
int v4l2_subdev_g_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *cs);
int v4l2_subdev_try_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *cs);
int v4l2_subdev_s_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *cs);
int v4l2_subdev_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
int v4l2_subdev_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl);

#endif
