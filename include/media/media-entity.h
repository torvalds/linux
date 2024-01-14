/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Media entity
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef _MEDIA_ENTITY_H
#define _MEDIA_ENTITY_H

#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/container_of.h>
#include <linux/fwnode.h>
#include <linux/list.h>
#include <linux/media.h>
#include <linux/minmax.h>
#include <linux/types.h>

/* Enums used internally at the media controller to represent graphs */

/**
 * enum media_gobj_type - type of a graph object
 *
 * @MEDIA_GRAPH_ENTITY:		Identify a media entity
 * @MEDIA_GRAPH_PAD:		Identify a media pad
 * @MEDIA_GRAPH_LINK:		Identify a media link
 * @MEDIA_GRAPH_INTF_DEVNODE:	Identify a media Kernel API interface via
 *				a device node
 */
enum media_gobj_type {
	MEDIA_GRAPH_ENTITY,
	MEDIA_GRAPH_PAD,
	MEDIA_GRAPH_LINK,
	MEDIA_GRAPH_INTF_DEVNODE,
};

#define MEDIA_BITS_PER_TYPE		8
#define MEDIA_BITS_PER_ID		(32 - MEDIA_BITS_PER_TYPE)
#define MEDIA_ID_MASK			 GENMASK_ULL(MEDIA_BITS_PER_ID - 1, 0)

/* Structs to represent the objects that belong to a media graph */

/**
 * struct media_gobj - Define a graph object.
 *
 * @mdev:	Pointer to the struct &media_device that owns the object
 * @id:		Non-zero object ID identifier. The ID should be unique
 *		inside a media_device, as it is composed by
 *		%MEDIA_BITS_PER_TYPE to store the type plus
 *		%MEDIA_BITS_PER_ID to store the ID
 * @list:	List entry stored in one of the per-type mdev object lists
 *
 * All objects on the media graph should have this struct embedded
 */
struct media_gobj {
	struct media_device	*mdev;
	u32			id;
	struct list_head	list;
};

#define MEDIA_ENTITY_ENUM_MAX_DEPTH	16

/**
 * struct media_entity_enum - An enumeration of media entities.
 *
 * @bmap:	Bit map in which each bit represents one entity at struct
 *		media_entity->internal_idx.
 * @idx_max:	Number of bits in bmap
 */
struct media_entity_enum {
	unsigned long *bmap;
	int idx_max;
};

/**
 * struct media_graph - Media graph traversal state
 *
 * @stack:		Graph traversal stack; the stack contains information
 *			on the path the media entities to be walked and the
 *			links through which they were reached.
 * @stack.entity:	pointer to &struct media_entity at the graph.
 * @stack.link:		pointer to &struct list_head.
 * @ent_enum:		Visited entities
 * @top:		The top of the stack
 */
struct media_graph {
	struct {
		struct media_entity *entity;
		struct list_head *link;
	} stack[MEDIA_ENTITY_ENUM_MAX_DEPTH];

	struct media_entity_enum ent_enum;
	int top;
};

/**
 * struct media_pipeline - Media pipeline related information
 *
 * @allocated:		Media pipeline allocated and freed by the framework
 * @mdev:		The media device the pipeline is part of
 * @pads:		List of media_pipeline_pad
 * @start_count:	Media pipeline start - stop count
 */
struct media_pipeline {
	bool allocated;
	struct media_device *mdev;
	struct list_head pads;
	int start_count;
};

/**
 * struct media_pipeline_pad - A pad part of a media pipeline
 *
 * @list:		Entry in the media_pad pads list
 * @pipe:		The media_pipeline that the pad is part of
 * @pad:		The media pad
 *
 * This structure associate a pad with a media pipeline. Instances of
 * media_pipeline_pad are created by media_pipeline_start() when it builds the
 * pipeline, and stored in the &media_pad.pads list. media_pipeline_stop()
 * removes the entries from the list and deletes them.
 */
struct media_pipeline_pad {
	struct list_head list;
	struct media_pipeline *pipe;
	struct media_pad *pad;
};

/**
 * struct media_link - A link object part of a media graph.
 *
 * @graph_obj:	Embedded structure containing the media object common data
 * @list:	Linked list associated with an entity or an interface that
 *		owns the link.
 * @gobj0:	Part of a union. Used to get the pointer for the first
 *		graph_object of the link.
 * @source:	Part of a union. Used only if the first object (gobj0) is
 *		a pad. In that case, it represents the source pad.
 * @intf:	Part of a union. Used only if the first object (gobj0) is
 *		an interface.
 * @gobj1:	Part of a union. Used to get the pointer for the second
 *		graph_object of the link.
 * @sink:	Part of a union. Used only if the second object (gobj1) is
 *		a pad. In that case, it represents the sink pad.
 * @entity:	Part of a union. Used only if the second object (gobj1) is
 *		an entity.
 * @reverse:	Pointer to the link for the reverse direction of a pad to pad
 *		link.
 * @flags:	Link flags, as defined in uapi/media.h (MEDIA_LNK_FL_*)
 * @is_backlink: Indicate if the link is a backlink.
 */
struct media_link {
	struct media_gobj graph_obj;
	struct list_head list;
	union {
		struct media_gobj *gobj0;
		struct media_pad *source;
		struct media_interface *intf;
	};
	union {
		struct media_gobj *gobj1;
		struct media_pad *sink;
		struct media_entity *entity;
	};
	struct media_link *reverse;
	unsigned long flags;
	bool is_backlink;
};

/**
 * enum media_pad_signal_type - type of the signal inside a media pad
 *
 * @PAD_SIGNAL_DEFAULT:
 *	Default signal. Use this when all inputs or all outputs are
 *	uniquely identified by the pad number.
 * @PAD_SIGNAL_ANALOG:
 *	The pad contains an analog signal. It can be Radio Frequency,
 *	Intermediate Frequency, a baseband signal or sub-carriers.
 *	Tuner inputs, IF-PLL demodulators, composite and s-video signals
 *	should use it.
 * @PAD_SIGNAL_DV:
 *	Contains a digital video signal, with can be a bitstream of samples
 *	taken from an analog TV video source. On such case, it usually
 *	contains the VBI data on it.
 * @PAD_SIGNAL_AUDIO:
 *	Contains an Intermediate Frequency analog signal from an audio
 *	sub-carrier or an audio bitstream. IF signals are provided by tuners
 *	and consumed by	audio AM/FM decoders. Bitstream audio is provided by
 *	an audio decoder.
 */
enum media_pad_signal_type {
	PAD_SIGNAL_DEFAULT = 0,
	PAD_SIGNAL_ANALOG,
	PAD_SIGNAL_DV,
	PAD_SIGNAL_AUDIO,
};

/**
 * struct media_pad - A media pad graph object.
 *
 * @graph_obj:	Embedded structure containing the media object common data
 * @entity:	Entity this pad belongs to
 * @index:	Pad index in the entity pads array, numbered from 0 to n
 * @num_links:	Number of links connected to this pad
 * @sig_type:	Type of the signal inside a media pad
 * @flags:	Pad flags, as defined in
 *		:ref:`include/uapi/linux/media.h <media_header>`
 *		(seek for ``MEDIA_PAD_FL_*``)
 * @pipe:	Pipeline this pad belongs to. Use media_entity_pipeline() to
 *		access this field.
 */
struct media_pad {
	struct media_gobj graph_obj;	/* must be first field in struct */
	struct media_entity *entity;
	u16 index;
	u16 num_links;
	enum media_pad_signal_type sig_type;
	unsigned long flags;

	/*
	 * The fields below are private, and should only be accessed via
	 * appropriate functions.
	 */
	struct media_pipeline *pipe;
};

/**
 * struct media_entity_operations - Media entity operations
 * @get_fwnode_pad:	Return the pad number based on a fwnode endpoint or
 *			a negative value on error. This operation can be used
 *			to map a fwnode to a media pad number. Optional.
 * @link_setup:		Notify the entity of link changes. The operation can
 *			return an error, in which case link setup will be
 *			cancelled. Optional.
 * @link_validate:	Return whether a link is valid from the entity point of
 *			view. The media_pipeline_start() function
 *			validates all links by calling this operation. Optional.
 * @has_pad_interdep:	Return whether a two pads inside the entity are
 *			interdependent. If two pads are interdependent they are
 *			part of the same pipeline and enabling one of the pads
 *			means that the other pad will become "locked" and
 *			doesn't allow configuration changes. pad0 and pad1 are
 *			guaranteed to not both be sinks or sources.
 *			Optional: If the operation isn't implemented all pads
 *			will be considered as interdependent.
 *
 * .. note::
 *
 *    Those these callbacks are called with struct &media_device.graph_mutex
 *    mutex held.
 */
struct media_entity_operations {
	int (*get_fwnode_pad)(struct media_entity *entity,
			      struct fwnode_endpoint *endpoint);
	int (*link_setup)(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags);
	int (*link_validate)(struct media_link *link);
	bool (*has_pad_interdep)(struct media_entity *entity, unsigned int pad0,
				 unsigned int pad1);
};

/**
 * enum media_entity_type - Media entity type
 *
 * @MEDIA_ENTITY_TYPE_BASE:
 *	The entity isn't embedded in another subsystem structure.
 * @MEDIA_ENTITY_TYPE_VIDEO_DEVICE:
 *	The entity is embedded in a struct video_device instance.
 * @MEDIA_ENTITY_TYPE_V4L2_SUBDEV:
 *	The entity is embedded in a struct v4l2_subdev instance.
 *
 * Media entity objects are often not instantiated directly, but the media
 * entity structure is inherited by (through embedding) other subsystem-specific
 * structures. The media entity type identifies the type of the subclass
 * structure that implements a media entity instance.
 *
 * This allows runtime type identification of media entities and safe casting to
 * the correct object type. For instance, a media entity structure instance
 * embedded in a v4l2_subdev structure instance will have the type
 * %MEDIA_ENTITY_TYPE_V4L2_SUBDEV and can safely be cast to a &v4l2_subdev
 * structure using the container_of() macro.
 */
enum media_entity_type {
	MEDIA_ENTITY_TYPE_BASE,
	MEDIA_ENTITY_TYPE_VIDEO_DEVICE,
	MEDIA_ENTITY_TYPE_V4L2_SUBDEV,
};

/**
 * struct media_entity - A media entity graph object.
 *
 * @graph_obj:	Embedded structure containing the media object common data.
 * @name:	Entity name.
 * @obj_type:	Type of the object that implements the media_entity.
 * @function:	Entity main function, as defined in
 *		:ref:`include/uapi/linux/media.h <media_header>`
 *		(seek for ``MEDIA_ENT_F_*``)
 * @flags:	Entity flags, as defined in
 *		:ref:`include/uapi/linux/media.h <media_header>`
 *		(seek for ``MEDIA_ENT_FL_*``)
 * @num_pads:	Number of sink and source pads.
 * @num_links:	Total number of links, forward and back, enabled and disabled.
 * @num_backlinks: Number of backlinks
 * @internal_idx: An unique internal entity specific number. The numbers are
 *		re-used if entities are unregistered or registered again.
 * @pads:	Pads array with the size defined by @num_pads.
 * @links:	List of data links.
 * @ops:	Entity operations.
 * @use_count:	Use count for the entity.
 * @info:	Union with devnode information.  Kept just for backward
 *		compatibility.
 * @info.dev:	Contains device major and minor info.
 * @info.dev.major: device node major, if the device is a devnode.
 * @info.dev.minor: device node minor, if the device is a devnode.
 * @major:	Devnode major number (zero if not applicable). Kept just
 *		for backward compatibility.
 * @minor:	Devnode minor number (zero if not applicable). Kept just
 *		for backward compatibility.
 *
 * .. note::
 *
 *    The @use_count reference count must never be negative, but is a signed
 *    integer on purpose: a simple ``WARN_ON(<0)`` check can be used to detect
 *    reference count bugs that would make it negative.
 */
struct media_entity {
	struct media_gobj graph_obj;	/* must be first field in struct */
	const char *name;
	enum media_entity_type obj_type;
	u32 function;
	unsigned long flags;

	u16 num_pads;
	u16 num_links;
	u16 num_backlinks;
	int internal_idx;

	struct media_pad *pads;
	struct list_head links;

	const struct media_entity_operations *ops;

	int use_count;

	union {
		struct {
			u32 major;
			u32 minor;
		} dev;
	} info;
};

/**
 * media_entity_for_each_pad - Iterate on all pads in an entity
 * @entity: The entity the pads belong to
 * @iter: The iterator pad
 *
 * Iterate on all pads in a media entity.
 */
#define media_entity_for_each_pad(entity, iter)			\
	for (iter = (entity)->pads;				\
	     iter < &(entity)->pads[(entity)->num_pads];	\
	     ++iter)

/**
 * struct media_interface - A media interface graph object.
 *
 * @graph_obj:		embedded graph object
 * @links:		List of links pointing to graph entities
 * @type:		Type of the interface as defined in
 *			:ref:`include/uapi/linux/media.h <media_header>`
 *			(seek for ``MEDIA_INTF_T_*``)
 * @flags:		Interface flags as defined in
 *			:ref:`include/uapi/linux/media.h <media_header>`
 *			(seek for ``MEDIA_INTF_FL_*``)
 *
 * .. note::
 *
 *    Currently, no flags for &media_interface is defined.
 */
struct media_interface {
	struct media_gobj		graph_obj;
	struct list_head		links;
	u32				type;
	u32				flags;
};

/**
 * struct media_intf_devnode - A media interface via a device node.
 *
 * @intf:	embedded interface object
 * @major:	Major number of a device node
 * @minor:	Minor number of a device node
 */
struct media_intf_devnode {
	struct media_interface		intf;

	/* Should match the fields at media_v2_intf_devnode */
	u32				major;
	u32				minor;
};

/**
 * media_entity_id() - return the media entity graph object id
 *
 * @entity:	pointer to &media_entity
 */
static inline u32 media_entity_id(struct media_entity *entity)
{
	return entity->graph_obj.id;
}

/**
 * media_type() - return the media object type
 *
 * @gobj:	Pointer to the struct &media_gobj graph object
 */
static inline enum media_gobj_type media_type(struct media_gobj *gobj)
{
	return gobj->id >> MEDIA_BITS_PER_ID;
}

/**
 * media_id() - return the media object ID
 *
 * @gobj:	Pointer to the struct &media_gobj graph object
 */
static inline u32 media_id(struct media_gobj *gobj)
{
	return gobj->id & MEDIA_ID_MASK;
}

/**
 * media_gobj_gen_id() - encapsulates type and ID on at the object ID
 *
 * @type:	object type as define at enum &media_gobj_type.
 * @local_id:	next ID, from struct &media_device.id.
 */
static inline u32 media_gobj_gen_id(enum media_gobj_type type, u64 local_id)
{
	u32 id;

	id = type << MEDIA_BITS_PER_ID;
	id |= local_id & MEDIA_ID_MASK;

	return id;
}

/**
 * is_media_entity_v4l2_video_device() - Check if the entity is a video_device
 * @entity:	pointer to entity
 *
 * Return: %true if the entity is an instance of a video_device object and can
 * safely be cast to a struct video_device using the container_of() macro, or
 * %false otherwise.
 */
static inline bool is_media_entity_v4l2_video_device(struct media_entity *entity)
{
	return entity && entity->obj_type == MEDIA_ENTITY_TYPE_VIDEO_DEVICE;
}

/**
 * is_media_entity_v4l2_subdev() - Check if the entity is a v4l2_subdev
 * @entity:	pointer to entity
 *
 * Return: %true if the entity is an instance of a &v4l2_subdev object and can
 * safely be cast to a struct &v4l2_subdev using the container_of() macro, or
 * %false otherwise.
 */
static inline bool is_media_entity_v4l2_subdev(struct media_entity *entity)
{
	return entity && entity->obj_type == MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
}

/**
 * media_entity_enum_init - Initialise an entity enumeration
 *
 * @ent_enum: Entity enumeration to be initialised
 * @mdev: The related media device
 *
 * Return: zero on success or a negative error code.
 */
__must_check int media_entity_enum_init(struct media_entity_enum *ent_enum,
					struct media_device *mdev);

/**
 * media_entity_enum_cleanup - Release resources of an entity enumeration
 *
 * @ent_enum: Entity enumeration to be released
 */
void media_entity_enum_cleanup(struct media_entity_enum *ent_enum);

/**
 * media_entity_enum_zero - Clear the entire enum
 *
 * @ent_enum: Entity enumeration to be cleared
 */
static inline void media_entity_enum_zero(struct media_entity_enum *ent_enum)
{
	bitmap_zero(ent_enum->bmap, ent_enum->idx_max);
}

/**
 * media_entity_enum_set - Mark a single entity in the enum
 *
 * @ent_enum: Entity enumeration
 * @entity: Entity to be marked
 */
static inline void media_entity_enum_set(struct media_entity_enum *ent_enum,
					 struct media_entity *entity)
{
	if (WARN_ON(entity->internal_idx >= ent_enum->idx_max))
		return;

	__set_bit(entity->internal_idx, ent_enum->bmap);
}

/**
 * media_entity_enum_clear - Unmark a single entity in the enum
 *
 * @ent_enum: Entity enumeration
 * @entity: Entity to be unmarked
 */
static inline void media_entity_enum_clear(struct media_entity_enum *ent_enum,
					   struct media_entity *entity)
{
	if (WARN_ON(entity->internal_idx >= ent_enum->idx_max))
		return;

	__clear_bit(entity->internal_idx, ent_enum->bmap);
}

/**
 * media_entity_enum_test - Test whether the entity is marked
 *
 * @ent_enum: Entity enumeration
 * @entity: Entity to be tested
 *
 * Returns %true if the entity was marked.
 */
static inline bool media_entity_enum_test(struct media_entity_enum *ent_enum,
					  struct media_entity *entity)
{
	if (WARN_ON(entity->internal_idx >= ent_enum->idx_max))
		return true;

	return test_bit(entity->internal_idx, ent_enum->bmap);
}

/**
 * media_entity_enum_test_and_set - Test whether the entity is marked,
 *	and mark it
 *
 * @ent_enum: Entity enumeration
 * @entity: Entity to be tested
 *
 * Returns %true if the entity was marked, and mark it before doing so.
 */
static inline bool
media_entity_enum_test_and_set(struct media_entity_enum *ent_enum,
			       struct media_entity *entity)
{
	if (WARN_ON(entity->internal_idx >= ent_enum->idx_max))
		return true;

	return __test_and_set_bit(entity->internal_idx, ent_enum->bmap);
}

/**
 * media_entity_enum_empty - Test whether the entire enum is empty
 *
 * @ent_enum: Entity enumeration
 *
 * Return: %true if the entity was empty.
 */
static inline bool media_entity_enum_empty(struct media_entity_enum *ent_enum)
{
	return bitmap_empty(ent_enum->bmap, ent_enum->idx_max);
}

/**
 * media_entity_enum_intersects - Test whether two enums intersect
 *
 * @ent_enum1: First entity enumeration
 * @ent_enum2: Second entity enumeration
 *
 * Return: %true if entity enumerations @ent_enum1 and @ent_enum2 intersect,
 * otherwise %false.
 */
static inline bool media_entity_enum_intersects(
	struct media_entity_enum *ent_enum1,
	struct media_entity_enum *ent_enum2)
{
	WARN_ON(ent_enum1->idx_max != ent_enum2->idx_max);

	return bitmap_intersects(ent_enum1->bmap, ent_enum2->bmap,
				 min(ent_enum1->idx_max, ent_enum2->idx_max));
}

/**
 * gobj_to_entity - returns the struct &media_entity pointer from the
 *	@gobj contained on it.
 *
 * @gobj: Pointer to the struct &media_gobj graph object
 */
#define gobj_to_entity(gobj) \
		container_of(gobj, struct media_entity, graph_obj)

/**
 * gobj_to_pad - returns the struct &media_pad pointer from the
 *	@gobj contained on it.
 *
 * @gobj: Pointer to the struct &media_gobj graph object
 */
#define gobj_to_pad(gobj) \
		container_of(gobj, struct media_pad, graph_obj)

/**
 * gobj_to_link - returns the struct &media_link pointer from the
 *	@gobj contained on it.
 *
 * @gobj: Pointer to the struct &media_gobj graph object
 */
#define gobj_to_link(gobj) \
		container_of(gobj, struct media_link, graph_obj)

/**
 * gobj_to_intf - returns the struct &media_interface pointer from the
 *	@gobj contained on it.
 *
 * @gobj: Pointer to the struct &media_gobj graph object
 */
#define gobj_to_intf(gobj) \
		container_of(gobj, struct media_interface, graph_obj)

/**
 * intf_to_devnode - returns the struct media_intf_devnode pointer from the
 *	@intf contained on it.
 *
 * @intf: Pointer to struct &media_intf_devnode
 */
#define intf_to_devnode(intf) \
		container_of(intf, struct media_intf_devnode, intf)

/**
 *  media_gobj_create - Initialize a graph object
 *
 * @mdev:	Pointer to the &media_device that contains the object
 * @type:	Type of the object
 * @gobj:	Pointer to the struct &media_gobj graph object
 *
 * This routine initializes the embedded struct &media_gobj inside a
 * media graph object. It is called automatically if ``media_*_create``
 * function calls are used. However, if the object (entity, link, pad,
 * interface) is embedded on some other object, this function should be
 * called before registering the object at the media controller.
 */
void media_gobj_create(struct media_device *mdev,
		    enum media_gobj_type type,
		    struct media_gobj *gobj);

/**
 *  media_gobj_destroy - Stop using a graph object on a media device
 *
 * @gobj:	Pointer to the struct &media_gobj graph object
 *
 * This should be called by all routines like media_device_unregister()
 * that remove/destroy media graph objects.
 */
void media_gobj_destroy(struct media_gobj *gobj);

/**
 * media_entity_pads_init() - Initialize the entity pads
 *
 * @entity:	entity where the pads belong
 * @num_pads:	total number of sink and source pads
 * @pads:	Array of @num_pads pads.
 *
 * The pads array is managed by the entity driver and passed to
 * media_entity_pads_init() where its pointer will be stored in the
 * &media_entity structure.
 *
 * If no pads are needed, drivers could either directly fill
 * &media_entity->num_pads with 0 and &media_entity->pads with %NULL or call
 * this function that will do the same.
 *
 * As the number of pads is known in advance, the pads array is not allocated
 * dynamically but is managed by the entity driver. Most drivers will embed the
 * pads array in a driver-specific structure, avoiding dynamic allocation.
 *
 * Drivers must set the direction of every pad in the pads array before calling
 * media_entity_pads_init(). The function will initialize the other pads fields.
 */
int media_entity_pads_init(struct media_entity *entity, u16 num_pads,
		      struct media_pad *pads);

/**
 * media_entity_cleanup() - free resources associated with an entity
 *
 * @entity:	entity where the pads belong
 *
 * This function must be called during the cleanup phase after unregistering
 * the entity (currently, it does nothing).
 *
 * Calling media_entity_cleanup() on a media_entity whose memory has been
 * zeroed but that has not been initialized with media_entity_pad_init() is
 * valid and is a no-op.
 */
#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)
static inline void media_entity_cleanup(struct media_entity *entity) {}
#else
#define media_entity_cleanup(entity) do { } while (false)
#endif

/**
 * media_get_pad_index() - retrieves a pad index from an entity
 *
 * @entity:	entity where the pads belong
 * @is_sink:	true if the pad is a sink, false if it is a source
 * @sig_type:	type of signal of the pad to be search
 *
 * This helper function finds the first pad index inside an entity that
 * satisfies both @is_sink and @sig_type conditions.
 *
 * Return:
 *
 * On success, return the pad number. If the pad was not found or the media
 * entity is a NULL pointer, return -EINVAL.
 */
int media_get_pad_index(struct media_entity *entity, bool is_sink,
			enum media_pad_signal_type sig_type);

/**
 * media_create_pad_link() - creates a link between two entities.
 *
 * @source:	pointer to &media_entity of the source pad.
 * @source_pad:	number of the source pad in the pads array
 * @sink:	pointer to &media_entity of the sink pad.
 * @sink_pad:	number of the sink pad in the pads array.
 * @flags:	Link flags, as defined in
 *		:ref:`include/uapi/linux/media.h <media_header>`
 *		( seek for ``MEDIA_LNK_FL_*``)
 *
 * Valid values for flags:
 *
 * %MEDIA_LNK_FL_ENABLED
 *   Indicates that the link is enabled and can be used to transfer media data.
 *   When two or more links target a sink pad, only one of them can be
 *   enabled at a time.
 *
 * %MEDIA_LNK_FL_IMMUTABLE
 *   Indicates that the link enabled state can't be modified at runtime. If
 *   %MEDIA_LNK_FL_IMMUTABLE is set, then %MEDIA_LNK_FL_ENABLED must also be
 *   set, since an immutable link is always enabled.
 *
 * .. note::
 *
 *    Before calling this function, media_entity_pads_init() and
 *    media_device_register_entity() should be called previously for both ends.
 */
__must_check int media_create_pad_link(struct media_entity *source,
			u16 source_pad, struct media_entity *sink,
			u16 sink_pad, u32 flags);

/**
 * media_create_pad_links() - creates a link between two entities.
 *
 * @mdev: Pointer to the media_device that contains the object
 * @source_function: Function of the source entities. Used only if @source is
 *	NULL.
 * @source: pointer to &media_entity of the source pad. If NULL, it will use
 *	all entities that matches the @sink_function.
 * @source_pad: number of the source pad in the pads array
 * @sink_function: Function of the sink entities. Used only if @sink is NULL.
 * @sink: pointer to &media_entity of the sink pad. If NULL, it will use
 *	all entities that matches the @sink_function.
 * @sink_pad: number of the sink pad in the pads array.
 * @flags: Link flags, as defined in include/uapi/linux/media.h.
 * @allow_both_undefined: if %true, then both @source and @sink can be NULL.
 *	In such case, it will create a crossbar between all entities that
 *	matches @source_function to all entities that matches @sink_function.
 *	If %false, it will return 0 and won't create any link if both @source
 *	and @sink are NULL.
 *
 * Valid values for flags:
 *
 * A %MEDIA_LNK_FL_ENABLED flag indicates that the link is enabled and can be
 *	used to transfer media data. If multiple links are created and this
 *	flag is passed as an argument, only the first created link will have
 *	this flag.
 *
 * A %MEDIA_LNK_FL_IMMUTABLE flag indicates that the link enabled state can't
 *	be modified at runtime. If %MEDIA_LNK_FL_IMMUTABLE is set, then
 *	%MEDIA_LNK_FL_ENABLED must also be set since an immutable link is
 *	always enabled.
 *
 * It is common for some devices to have multiple source and/or sink entities
 * of the same type that should be linked. While media_create_pad_link()
 * creates link by link, this function is meant to allow 1:n, n:1 and even
 * cross-bar (n:n) links.
 *
 * .. note::
 *
 *    Before calling this function, media_entity_pads_init() and
 *    media_device_register_entity() should be called previously for the
 *    entities to be linked.
 */
int media_create_pad_links(const struct media_device *mdev,
			   const u32 source_function,
			   struct media_entity *source,
			   const u16 source_pad,
			   const u32 sink_function,
			   struct media_entity *sink,
			   const u16 sink_pad,
			   u32 flags,
			   const bool allow_both_undefined);

void __media_entity_remove_links(struct media_entity *entity);

/**
 * media_entity_remove_links() - remove all links associated with an entity
 *
 * @entity:	pointer to &media_entity
 *
 * .. note::
 *
 *    This is called automatically when an entity is unregistered via
 *    media_device_register_entity().
 */
void media_entity_remove_links(struct media_entity *entity);

/**
 * __media_entity_setup_link - Configure a media link without locking
 * @link: The link being configured
 * @flags: Link configuration flags
 *
 * The bulk of link setup is handled by the two entities connected through the
 * link. This function notifies both entities of the link configuration change.
 *
 * If the link is immutable or if the current and new configuration are
 * identical, return immediately.
 *
 * The user is expected to hold link->source->parent->mutex. If not,
 * media_entity_setup_link() should be used instead.
 */
int __media_entity_setup_link(struct media_link *link, u32 flags);

/**
 * media_entity_setup_link() - changes the link flags properties in runtime
 *
 * @link:	pointer to &media_link
 * @flags:	the requested new link flags
 *
 * The only configurable property is the %MEDIA_LNK_FL_ENABLED link flag
 * to enable/disable a link. Links marked with the
 * %MEDIA_LNK_FL_IMMUTABLE link flag can not be enabled or disabled.
 *
 * When a link is enabled or disabled, the media framework calls the
 * link_setup operation for the two entities at the source and sink of the
 * link, in that order. If the second link_setup call fails, another
 * link_setup call is made on the first entity to restore the original link
 * flags.
 *
 * Media device drivers can be notified of link setup operations by setting the
 * &media_device.link_notify pointer to a callback function. If provided, the
 * notification callback will be called before enabling and after disabling
 * links.
 *
 * Entity drivers must implement the link_setup operation if any of their links
 * is non-immutable. The operation must either configure the hardware or store
 * the configuration information to be applied later.
 *
 * Link configuration must not have any side effect on other links. If an
 * enabled link at a sink pad prevents another link at the same pad from
 * being enabled, the link_setup operation must return %-EBUSY and can't
 * implicitly disable the first enabled link.
 *
 * .. note::
 *
 *    The valid values of the flags for the link is the same as described
 *    on media_create_pad_link(), for pad to pad links or the same as described
 *    on media_create_intf_link(), for interface to entity links.
 */
int media_entity_setup_link(struct media_link *link, u32 flags);

/**
 * media_entity_find_link - Find a link between two pads
 * @source: Source pad
 * @sink: Sink pad
 *
 * Return: returns a pointer to the link between the two entities. If no
 * such link exists, return %NULL.
 */
struct media_link *media_entity_find_link(struct media_pad *source,
		struct media_pad *sink);

/**
 * media_pad_remote_pad_first - Find the first pad at the remote end of a link
 * @pad: Pad at the local end of the link
 *
 * Search for a remote pad connected to the given pad by iterating over all
 * links originating or terminating at that pad until an enabled link is found.
 *
 * Return: returns a pointer to the pad at the remote end of the first found
 * enabled link, or %NULL if no enabled link has been found.
 */
struct media_pad *media_pad_remote_pad_first(const struct media_pad *pad);

/**
 * media_pad_remote_pad_unique - Find a remote pad connected to a pad
 * @pad: The pad
 *
 * Search for and return a remote pad connected to @pad through an enabled
 * link. If multiple (or no) remote pads are found, an error is returned.
 *
 * The uniqueness constraint makes this helper function suitable for entities
 * that support a single active source at a time on a given pad.
 *
 * Return: A pointer to the remote pad, or one of the following error pointers
 * if an error occurs:
 *
 * * -ENOTUNIQ - Multiple links are enabled
 * * -ENOLINK - No connected pad found
 */
struct media_pad *media_pad_remote_pad_unique(const struct media_pad *pad);

/**
 * media_entity_remote_pad_unique - Find a remote pad connected to an entity
 * @entity: The entity
 * @type: The type of pad to find (MEDIA_PAD_FL_SINK or MEDIA_PAD_FL_SOURCE)
 *
 * Search for and return a remote pad of @type connected to @entity through an
 * enabled link. If multiple (or no) remote pads match these criteria, an error
 * is returned.
 *
 * The uniqueness constraint makes this helper function suitable for entities
 * that support a single active source or sink at a time.
 *
 * Return: A pointer to the remote pad, or one of the following error pointers
 * if an error occurs:
 *
 * * -ENOTUNIQ - Multiple links are enabled
 * * -ENOLINK - No connected pad found
 */
struct media_pad *
media_entity_remote_pad_unique(const struct media_entity *entity,
			       unsigned int type);

/**
 * media_entity_remote_source_pad_unique - Find a remote source pad connected to
 *	an entity
 * @entity: The entity
 *
 * Search for and return a remote source pad connected to @entity through an
 * enabled link. If multiple (or no) remote pads match these criteria, an error
 * is returned.
 *
 * The uniqueness constraint makes this helper function suitable for entities
 * that support a single active source at a time.
 *
 * Return: A pointer to the remote pad, or one of the following error pointers
 * if an error occurs:
 *
 * * -ENOTUNIQ - Multiple links are enabled
 * * -ENOLINK - No connected pad found
 */
static inline struct media_pad *
media_entity_remote_source_pad_unique(const struct media_entity *entity)
{
	return media_entity_remote_pad_unique(entity, MEDIA_PAD_FL_SOURCE);
}

/**
 * media_pad_is_streaming - Test if a pad is part of a streaming pipeline
 * @pad: The pad
 *
 * Return: True if the pad is part of a pipeline started with the
 * media_pipeline_start() function, false otherwise.
 */
static inline bool media_pad_is_streaming(const struct media_pad *pad)
{
	return pad->pipe;
}

/**
 * media_entity_is_streaming - Test if an entity is part of a streaming pipeline
 * @entity: The entity
 *
 * Return: True if the entity is part of a pipeline started with the
 * media_pipeline_start() function, false otherwise.
 */
static inline bool media_entity_is_streaming(const struct media_entity *entity)
{
	struct media_pad *pad;

	media_entity_for_each_pad(entity, pad) {
		if (media_pad_is_streaming(pad))
			return true;
	}

	return false;
}

/**
 * media_entity_pipeline - Get the media pipeline an entity is part of
 * @entity: The entity
 *
 * DEPRECATED: use media_pad_pipeline() instead.
 *
 * This function returns the media pipeline that an entity has been associated
 * with when constructing the pipeline with media_pipeline_start(). The pointer
 * remains valid until media_pipeline_stop() is called.
 *
 * In general, entities can be part of multiple pipelines, when carrying
 * multiple streams (either on different pads, or on the same pad using
 * multiplexed streams). This function is to be used only for entities that
 * do not support multiple pipelines.
 *
 * Return: The media_pipeline the entity is part of, or NULL if the entity is
 * not part of any pipeline.
 */
struct media_pipeline *media_entity_pipeline(struct media_entity *entity);

/**
 * media_pad_pipeline - Get the media pipeline a pad is part of
 * @pad: The pad
 *
 * This function returns the media pipeline that a pad has been associated
 * with when constructing the pipeline with media_pipeline_start(). The pointer
 * remains valid until media_pipeline_stop() is called.
 *
 * Return: The media_pipeline the pad is part of, or NULL if the pad is
 * not part of any pipeline.
 */
struct media_pipeline *media_pad_pipeline(struct media_pad *pad);

/**
 * media_entity_get_fwnode_pad - Get pad number from fwnode
 *
 * @entity: The entity
 * @fwnode: Pointer to the fwnode_handle which should be used to find the pad
 * @direction_flags: Expected direction of the pad, as defined in
 *		     :ref:`include/uapi/linux/media.h <media_header>`
 *		     (seek for ``MEDIA_PAD_FL_*``)
 *
 * This function can be used to resolve the media pad number from
 * a fwnode. This is useful for devices which use more complex
 * mappings of media pads.
 *
 * If the entity does not implement the get_fwnode_pad() operation
 * then this function searches the entity for the first pad that
 * matches the @direction_flags.
 *
 * Return: returns the pad number on success or a negative error code.
 */
int media_entity_get_fwnode_pad(struct media_entity *entity,
				struct fwnode_handle *fwnode,
				unsigned long direction_flags);

/**
 * media_graph_walk_init - Allocate resources used by graph walk.
 *
 * @graph: Media graph structure that will be used to walk the graph
 * @mdev: Pointer to the &media_device that contains the object
 *
 * The caller is required to hold the media_device graph_mutex during the graph
 * walk until the graph state is released.
 *
 * Returns zero on success or a negative error code otherwise.
 */
__must_check int media_graph_walk_init(
	struct media_graph *graph, struct media_device *mdev);

/**
 * media_graph_walk_cleanup - Release resources used by graph walk.
 *
 * @graph: Media graph structure that will be used to walk the graph
 */
void media_graph_walk_cleanup(struct media_graph *graph);

/**
 * media_graph_walk_start - Start walking the media graph at a
 *	given entity
 *
 * @graph: Media graph structure that will be used to walk the graph
 * @entity: Starting entity
 *
 * Before using this function, media_graph_walk_init() must be
 * used to allocate resources used for walking the graph. This
 * function initializes the graph traversal structure to walk the
 * entities graph starting at the given entity. The traversal
 * structure must not be modified by the caller during graph
 * traversal. After the graph walk, the resources must be released
 * using media_graph_walk_cleanup().
 */
void media_graph_walk_start(struct media_graph *graph,
			    struct media_entity *entity);

/**
 * media_graph_walk_next - Get the next entity in the graph
 * @graph: Media graph structure
 *
 * Perform a depth-first traversal of the given media entities graph.
 *
 * The graph structure must have been previously initialized with a call to
 * media_graph_walk_start().
 *
 * Return: returns the next entity in the graph or %NULL if the whole graph
 * have been traversed.
 */
struct media_entity *media_graph_walk_next(struct media_graph *graph);

/**
 * media_pipeline_start - Mark a pipeline as streaming
 * @pad: Starting pad
 * @pipe: Media pipeline to be assigned to all pads in the pipeline.
 *
 * Mark all pads connected to a given pad through enabled links, either
 * directly or indirectly, as streaming. The given pipeline object is assigned
 * to every pad in the pipeline and stored in the media_pad pipe field.
 *
 * Calls to this function can be nested, in which case the same number of
 * media_pipeline_stop() calls will be required to stop streaming. The
 * pipeline pointer must be identical for all nested calls to
 * media_pipeline_start().
 */
__must_check int media_pipeline_start(struct media_pad *pad,
				      struct media_pipeline *pipe);
/**
 * __media_pipeline_start - Mark a pipeline as streaming
 *
 * @pad: Starting pad
 * @pipe: Media pipeline to be assigned to all pads in the pipeline.
 *
 * ..note:: This is the non-locking version of media_pipeline_start()
 */
__must_check int __media_pipeline_start(struct media_pad *pad,
					struct media_pipeline *pipe);

/**
 * media_pipeline_stop - Mark a pipeline as not streaming
 * @pad: Starting pad
 *
 * Mark all pads connected to a given pads through enabled links, either
 * directly or indirectly, as not streaming. The media_pad pipe field is
 * reset to %NULL.
 *
 * If multiple calls to media_pipeline_start() have been made, the same
 * number of calls to this function are required to mark the pipeline as not
 * streaming.
 */
void media_pipeline_stop(struct media_pad *pad);

/**
 * __media_pipeline_stop - Mark a pipeline as not streaming
 *
 * @pad: Starting pad
 *
 * .. note:: This is the non-locking version of media_pipeline_stop()
 */
void __media_pipeline_stop(struct media_pad *pad);

/**
 * media_pipeline_alloc_start - Mark a pipeline as streaming
 * @pad: Starting pad
 *
 * media_pipeline_alloc_start() is similar to media_pipeline_start() but instead
 * of working on a given pipeline the function will use an existing pipeline if
 * the pad is already part of a pipeline, or allocate a new pipeline.
 *
 * Calls to media_pipeline_alloc_start() must be matched with
 * media_pipeline_stop().
 */
__must_check int media_pipeline_alloc_start(struct media_pad *pad);

/**
 * media_devnode_create() - creates and initializes a device node interface
 *
 * @mdev:	pointer to struct &media_device
 * @type:	type of the interface, as given by
 *		:ref:`include/uapi/linux/media.h <media_header>`
 *		( seek for ``MEDIA_INTF_T_*``) macros.
 * @flags:	Interface flags, as defined in
 *		:ref:`include/uapi/linux/media.h <media_header>`
 *		( seek for ``MEDIA_INTF_FL_*``)
 * @major:	Device node major number.
 * @minor:	Device node minor number.
 *
 * Return: if succeeded, returns a pointer to the newly allocated
 *	&media_intf_devnode pointer.
 *
 * .. note::
 *
 *    Currently, no flags for &media_interface is defined.
 */
struct media_intf_devnode *
__must_check media_devnode_create(struct media_device *mdev,
				  u32 type, u32 flags,
				  u32 major, u32 minor);
/**
 * media_devnode_remove() - removes a device node interface
 *
 * @devnode:	pointer to &media_intf_devnode to be freed.
 *
 * When a device node interface is removed, all links to it are automatically
 * removed.
 */
void media_devnode_remove(struct media_intf_devnode *devnode);

/**
 * media_create_intf_link() - creates a link between an entity and an interface
 *
 * @entity:	pointer to %media_entity
 * @intf:	pointer to %media_interface
 * @flags:	Link flags, as defined in
 *		:ref:`include/uapi/linux/media.h <media_header>`
 *		( seek for ``MEDIA_LNK_FL_*``)
 *
 *
 * Valid values for flags:
 *
 * %MEDIA_LNK_FL_ENABLED
 *   Indicates that the interface is connected to the entity hardware.
 *   That's the default value for interfaces. An interface may be disabled if
 *   the hardware is busy due to the usage of some other interface that it is
 *   currently controlling the hardware.
 *
 *   A typical example is an hybrid TV device that handle only one type of
 *   stream on a given time. So, when the digital TV is streaming,
 *   the V4L2 interfaces won't be enabled, as such device is not able to
 *   also stream analog TV or radio.
 *
 * .. note::
 *
 *    Before calling this function, media_devnode_create() should be called for
 *    the interface and media_device_register_entity() should be called for the
 *    interface that will be part of the link.
 */
struct media_link *
__must_check media_create_intf_link(struct media_entity *entity,
				    struct media_interface *intf,
				    u32 flags);
/**
 * __media_remove_intf_link() - remove a single interface link
 *
 * @link:	pointer to &media_link.
 *
 * .. note:: This is an unlocked version of media_remove_intf_link()
 */
void __media_remove_intf_link(struct media_link *link);

/**
 * media_remove_intf_link() - remove a single interface link
 *
 * @link:	pointer to &media_link.
 *
 * .. note:: Prefer to use this one, instead of __media_remove_intf_link()
 */
void media_remove_intf_link(struct media_link *link);

/**
 * __media_remove_intf_links() - remove all links associated with an interface
 *
 * @intf:	pointer to &media_interface
 *
 * .. note:: This is an unlocked version of media_remove_intf_links().
 */
void __media_remove_intf_links(struct media_interface *intf);

/**
 * media_remove_intf_links() - remove all links associated with an interface
 *
 * @intf:	pointer to &media_interface
 *
 * .. note::
 *
 *   #) This is called automatically when an entity is unregistered via
 *      media_device_register_entity() and by media_devnode_remove().
 *
 *   #) Prefer to use this one, instead of __media_remove_intf_links().
 */
void media_remove_intf_links(struct media_interface *intf);

/**
 * media_entity_call - Calls a struct media_entity_operations operation on
 *	an entity
 *
 * @entity: entity where the @operation will be called
 * @operation: type of the operation. Should be the name of a member of
 *	struct &media_entity_operations.
 *
 * This helper function will check if @operation is not %NULL. On such case,
 * it will issue a call to @operation\(@entity, @args\).
 */

#define media_entity_call(entity, operation, args...)			\
	(((entity)->ops && (entity)->ops->operation) ?			\
	 (entity)->ops->operation((entity) , ##args) : -ENOIOCTLCMD)

/**
 * media_create_ancillary_link() - create an ancillary link between two
 *				   instances of &media_entity
 *
 * @primary:	pointer to the primary &media_entity
 * @ancillary:	pointer to the ancillary &media_entity
 *
 * Create an ancillary link between two entities, indicating that they
 * represent two connected pieces of hardware that form a single logical unit.
 * A typical example is a camera lens controller being linked to the sensor that
 * it is supporting.
 *
 * The function sets both MEDIA_LNK_FL_ENABLED and MEDIA_LNK_FL_IMMUTABLE for
 * the new link.
 */
struct media_link *
media_create_ancillary_link(struct media_entity *primary,
			    struct media_entity *ancillary);

/**
 * __media_entity_next_link() - Iterate through a &media_entity's links
 *
 * @entity:	pointer to the &media_entity
 * @link:	pointer to a &media_link to hold the iterated values
 * @link_type:	one of the MEDIA_LNK_FL_LINK_TYPE flags
 *
 * Return the next link against an entity matching a specific link type. This
 * allows iteration through an entity's links whilst guaranteeing all of the
 * returned links are of the given type.
 */
struct media_link *__media_entity_next_link(struct media_entity *entity,
					    struct media_link *link,
					    unsigned long link_type);

/**
 * for_each_media_entity_data_link() - Iterate through an entity's data links
 *
 * @entity:	pointer to the &media_entity
 * @link:	pointer to a &media_link to hold the iterated values
 *
 * Iterate over a &media_entity's data links
 */
#define for_each_media_entity_data_link(entity, link)			\
	for (link = __media_entity_next_link(entity, NULL,		\
					     MEDIA_LNK_FL_DATA_LINK);	\
	     link;							\
	     link = __media_entity_next_link(entity, link,		\
					     MEDIA_LNK_FL_DATA_LINK))

#endif
