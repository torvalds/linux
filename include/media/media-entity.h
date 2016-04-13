/*
 * Media entity
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _MEDIA_ENTITY_H
#define _MEDIA_ENTITY_H

#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/media.h>

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
 * @mdev:	Pointer to the struct media_device that owns the object
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
 * struct media_entity_graph - Media graph traversal state
 *
 * @stack:		Graph traversal stack; the stack contains information
 *			on the path the media entities to be walked and the
 *			links through which they were reached.
 * @ent_enum:		Visited entities
 * @top:		The top of the stack
 */
struct media_entity_graph {
	struct {
		struct media_entity *entity;
		struct list_head *link;
	} stack[MEDIA_ENTITY_ENUM_MAX_DEPTH];

	struct media_entity_enum ent_enum;
	int top;
};

/*
 * struct media_pipeline - Media pipeline related information
 *
 * @streaming_count:	Streaming start count - streaming stop count
 * @graph:		Media graph walk during pipeline start / stop
 */
struct media_pipeline {
	int streaming_count;
	struct media_entity_graph graph;
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
 * @source:	Part of a union. Used only if the second object (gobj1) is
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
 * struct media_pad - A media pad graph object.
 *
 * @graph_obj:	Embedded structure containing the media object common data
 * @entity:	Entity this pad belongs to
 * @index:	Pad index in the entity pads array, numbered from 0 to n
 * @flags:	Pad flags, as defined in uapi/media.h (MEDIA_PAD_FL_*)
 */
struct media_pad {
	struct media_gobj graph_obj;	/* must be first field in struct */
	struct media_entity *entity;
	u16 index;
	unsigned long flags;
};

/**
 * struct media_entity_operations - Media entity operations
 * @link_setup:		Notify the entity of link changes. The operation can
 *			return an error, in which case link setup will be
 *			cancelled. Optional.
 * @link_validate:	Return whether a link is valid from the entity point of
 *			view. The media_entity_pipeline_start() function
 *			validates all links by calling this operation. Optional.
 */
struct media_entity_operations {
	int (*link_setup)(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags);
	int (*link_validate)(struct media_link *link);
};

/**
 * struct media_entity - A media entity graph object.
 *
 * @graph_obj:	Embedded structure containing the media object common data.
 * @name:	Entity name.
 * @function:	Entity main function, as defined in uapi/media.h
 *		(MEDIA_ENT_F_*)
 * @flags:	Entity flags, as defined in uapi/media.h (MEDIA_ENT_FL_*)
 * @num_pads:	Number of sink and source pads.
 * @num_links:	Total number of links, forward and back, enabled and disabled.
 * @num_backlinks: Number of backlinks
 * @internal_idx: An unique internal entity specific number. The numbers are
 *		re-used if entities are unregistered or registered again.
 * @pads:	Pads array with the size defined by @num_pads.
 * @links:	List of data links.
 * @ops:	Entity operations.
 * @stream_count: Stream count for the entity.
 * @use_count:	Use count for the entity.
 * @pipe:	Pipeline this entity belongs to.
 * @info:	Union with devnode information.  Kept just for backward
 *		compatibility.
 * @major:	Devnode major number (zero if not applicable). Kept just
 *		for backward compatibility.
 * @minor:	Devnode minor number (zero if not applicable). Kept just
 *		for backward compatibility.
 *
 * NOTE: @stream_count and @use_count reference counts must never be
 * negative, but are signed integers on purpose: a simple WARN_ON(<0) check
 * can be used to detect reference count bugs that would make them negative.
 */
struct media_entity {
	struct media_gobj graph_obj;	/* must be first field in struct */
	const char *name;
	u32 function;
	unsigned long flags;

	u16 num_pads;
	u16 num_links;
	u16 num_backlinks;
	int internal_idx;

	struct media_pad *pads;
	struct list_head links;

	const struct media_entity_operations *ops;

	/* Reference counts must never be negative, but are signed integers on
	 * purpose: a simple WARN_ON(<0) check can be used to detect reference
	 * count bugs that would make them negative.
	 */
	int stream_count;
	int use_count;

	struct media_pipeline *pipe;

	union {
		struct {
			u32 major;
			u32 minor;
		} dev;
	} info;
};

/**
 * struct media_interface - A media interface graph object.
 *
 * @graph_obj:		embedded graph object
 * @links:		List of links pointing to graph entities
 * @type:		Type of the interface as defined in the
 *			uapi/media/media.h header, e. g.
 *			MEDIA_INTF_T_*
 * @flags:		Interface flags as defined in uapi/media/media.h
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
 * @entity:	pointer to entity
 */
static inline u32 media_entity_id(struct media_entity *entity)
{
	return entity->graph_obj.id;
}

/**
 * media_type() - return the media object type
 *
 * @gobj:	pointer to the media graph object
 */
static inline enum media_gobj_type media_type(struct media_gobj *gobj)
{
	return gobj->id >> MEDIA_BITS_PER_ID;
}

/**
 * media_id() - return the media object ID
 *
 * @gobj:	pointer to the media graph object
 */
static inline u32 media_id(struct media_gobj *gobj)
{
	return gobj->id & MEDIA_ID_MASK;
}

/**
 * media_gobj_gen_id() - encapsulates type and ID on at the object ID
 *
 * @type:	object type as define at enum &media_gobj_type.
 * @local_id:	next ID, from struct &media_device.@id.
 */
static inline u32 media_gobj_gen_id(enum media_gobj_type type, u64 local_id)
{
	u32 id;

	id = type << MEDIA_BITS_PER_ID;
	id |= local_id & MEDIA_ID_MASK;

	return id;
}

/**
 * is_media_entity_v4l2_io() - identify if the entity main function
 *			       is a V4L2 I/O
 *
 * @entity:	pointer to entity
 *
 * Return: true if the entity main function is one of the V4L2 I/O types
 *	(video, VBI or SDR radio); false otherwise.
 */
static inline bool is_media_entity_v4l2_io(struct media_entity *entity)
{
	if (!entity)
		return false;

	switch (entity->function) {
	case MEDIA_ENT_F_IO_V4L:
	case MEDIA_ENT_F_IO_VBI:
	case MEDIA_ENT_F_IO_SWRADIO:
		return true;
	default:
		return false;
	}
}

/**
 * is_media_entity_v4l2_subdev - return true if the entity main function is
 *				 associated with the V4L2 API subdev usage
 *
 * @entity:	pointer to entity
 *
 * This is an ancillary function used by subdev-based V4L2 drivers.
 * It checks if the entity function is one of functions used by a V4L2 subdev,
 * e. g. camera-relatef functions, analog TV decoder, TV tuner, V4L2 DSPs.
 */
static inline bool is_media_entity_v4l2_subdev(struct media_entity *entity)
{
	if (!entity)
		return false;

	switch (entity->function) {
	case MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN:
	case MEDIA_ENT_F_CAM_SENSOR:
	case MEDIA_ENT_F_FLASH:
	case MEDIA_ENT_F_LENS:
	case MEDIA_ENT_F_ATV_DECODER:
	case MEDIA_ENT_F_TUNER:
		return true;

	default:
		return false;
	}
}

/**
 * __media_entity_enum_init - Initialise an entity enumeration
 *
 * @ent_enum: Entity enumeration to be initialised
 * @idx_max: Maximum number of entities in the enumeration
 *
 * Return: Returns zero on success or a negative error code.
 */
__must_check int __media_entity_enum_init(struct media_entity_enum *ent_enum,
					  int idx_max);

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
 * Returns true if the entity was marked.
 */
static inline bool media_entity_enum_test(struct media_entity_enum *ent_enum,
					  struct media_entity *entity)
{
	if (WARN_ON(entity->internal_idx >= ent_enum->idx_max))
		return true;

	return test_bit(entity->internal_idx, ent_enum->bmap);
}

/**
 * media_entity_enum_test - Test whether the entity is marked, and mark it
 *
 * @ent_enum: Entity enumeration
 * @entity: Entity to be tested
 *
 * Returns true if the entity was marked, and mark it before doing so.
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
 * Returns true if the entity was marked.
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
 * Returns true if entity enumerations e and f intersect, otherwise false.
 */
static inline bool media_entity_enum_intersects(
	struct media_entity_enum *ent_enum1,
	struct media_entity_enum *ent_enum2)
{
	WARN_ON(ent_enum1->idx_max != ent_enum2->idx_max);

	return bitmap_intersects(ent_enum1->bmap, ent_enum2->bmap,
				 min(ent_enum1->idx_max, ent_enum2->idx_max));
}

#define gobj_to_entity(gobj) \
		container_of(gobj, struct media_entity, graph_obj)

#define gobj_to_pad(gobj) \
		container_of(gobj, struct media_pad, graph_obj)

#define gobj_to_link(gobj) \
		container_of(gobj, struct media_link, graph_obj)

#define gobj_to_link(gobj) \
		container_of(gobj, struct media_link, graph_obj)

#define gobj_to_pad(gobj) \
		container_of(gobj, struct media_pad, graph_obj)

#define gobj_to_intf(gobj) \
		container_of(gobj, struct media_interface, graph_obj)

#define intf_to_devnode(intf) \
		container_of(intf, struct media_intf_devnode, intf)

/**
 *  media_gobj_create - Initialize a graph object
 *
 * @mdev:	Pointer to the media_device that contains the object
 * @type:	Type of the object
 * @gobj:	Pointer to the graph object
 *
 * This routine initializes the embedded struct media_gobj inside a
 * media graph object. It is called automatically if media_*_create()
 * calls are used. However, if the object (entity, link, pad, interface)
 * is embedded on some other object, this function should be called before
 * registering the object at the media controller.
 */
void media_gobj_create(struct media_device *mdev,
		    enum media_gobj_type type,
		    struct media_gobj *gobj);

/**
 *  media_gobj_destroy - Stop using a graph object on a media device
 *
 * @gobj:	Pointer to the graph object
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
 * media_entity_pads_init() where its pointer will be stored in the entity
 * structure.
 *
 * If no pads are needed, drivers could either directly fill
 * &media_entity->@num_pads with 0 and &media_entity->@pads with NULL or call
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
 */
static inline void media_entity_cleanup(struct media_entity *entity) {};

/**
 * media_create_pad_link() - creates a link between two entities.
 *
 * @source:	pointer to &media_entity of the source pad.
 * @source_pad:	number of the source pad in the pads array
 * @sink:	pointer to &media_entity of the sink pad.
 * @sink_pad:	number of the sink pad in the pads array.
 * @flags:	Link flags, as defined in include/uapi/linux/media.h.
 *
 * Valid values for flags:
 * A %MEDIA_LNK_FL_ENABLED flag indicates that the link is enabled and can be
 *	used to transfer media data. When two or more links target a sink pad,
 *	only one of them can be enabled at a time.
 *
 * A %MEDIA_LNK_FL_IMMUTABLE flag indicates that the link enabled state can't
 *	be modified at runtime. If %MEDIA_LNK_FL_IMMUTABLE is set, then
 *	%MEDIA_LNK_FL_ENABLED must also be set since an immutable link is
 *	always enabled.
 *
 * NOTE:
 *
 * Before calling this function, media_entity_pads_init() and
 * media_device_register_entity() should be called previously for both ends.
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
 * 	all entities that matches the @sink_function.
 * @source_pad: number of the source pad in the pads array
 * @sink_function: Function of the sink entities. Used only if @sink is NULL.
 * @sink: pointer to &media_entity of the sink pad. If NULL, it will use
 * 	all entities that matches the @sink_function.
 * @sink_pad: number of the sink pad in the pads array.
 * @flags: Link flags, as defined in include/uapi/linux/media.h.
 * @allow_both_undefined: if true, then both @source and @sink can be NULL.
 *	In such case, it will create a crossbar between all entities that
 *	matches @source_function to all entities that matches @sink_function.
 *	If false, it will return 0 and won't create any link if both @source
 *	and @sink are NULL.
 *
 * Valid values for flags:
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
 * NOTE: Before calling this function, media_entity_pads_init() and
 * media_device_register_entity() should be called previously for the entities
 * to be linked.
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
 * Note: this is called automatically when an entity is unregistered via
 * media_device_register_entity().
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
 * flag to enable/disable a link. Links marked with the
 * %MEDIA_LNK_FL_IMMUTABLE link flag can not be enabled or disabled.
 *
 * When a link is enabled or disabled, the media framework calls the
 * link_setup operation for the two entities at the source and sink of the
 * link, in that order. If the second link_setup call fails, another
 * link_setup call is made on the first entity to restore the original link
 * flags.
 *
 * Media device drivers can be notified of link setup operations by setting the
 * media_device::link_notify pointer to a callback function. If provided, the
 * notification callback will be called before enabling and after disabling
 * links.
 *
 * Entity drivers must implement the link_setup operation if any of their links
 * is non-immutable. The operation must either configure the hardware or store
 * the configuration information to be applied later.
 *
 * Link configuration must not have any side effect on other links. If an
 * enabled link at a sink pad prevents another link at the same pad from
 * being enabled, the link_setup operation must return -EBUSY and can't
 * implicitly disable the first enabled link.
 *
 * NOTE: the valid values of the flags for the link is the same as described
 * on media_create_pad_link(), for pad to pad links or the same as described
 * on media_create_intf_link(), for interface to entity links.
 */
int media_entity_setup_link(struct media_link *link, u32 flags);

/**
 * media_entity_find_link - Find a link between two pads
 * @source: Source pad
 * @sink: Sink pad
 *
 * Return a pointer to the link between the two entities. If no such link
 * exists, return NULL.
 */
struct media_link *media_entity_find_link(struct media_pad *source,
		struct media_pad *sink);

/**
 * media_entity_remote_pad - Find the pad at the remote end of a link
 * @pad: Pad at the local end of the link
 *
 * Search for a remote pad connected to the given pad by iterating over all
 * links originating or terminating at that pad until an enabled link is found.
 *
 * Return a pointer to the pad at the remote end of the first found enabled
 * link, or NULL if no enabled link has been found.
 */
struct media_pad *media_entity_remote_pad(struct media_pad *pad);

/**
 * media_entity_get - Get a reference to the parent module
 *
 * @entity: The entity
 *
 * Get a reference to the parent media device module.
 *
 * The function will return immediately if @entity is NULL.
 *
 * Return a pointer to the entity on success or NULL on failure.
 */
struct media_entity *media_entity_get(struct media_entity *entity);

__must_check int media_entity_graph_walk_init(
	struct media_entity_graph *graph, struct media_device *mdev);

/**
 * media_entity_graph_walk_cleanup - Release resources used by graph walk.
 *
 * @graph: Media graph structure that will be used to walk the graph
 */
void media_entity_graph_walk_cleanup(struct media_entity_graph *graph);

/**
 * media_entity_put - Release the reference to the parent module
 *
 * @entity: The entity
 *
 * Release the reference count acquired by media_entity_get().
 *
 * The function will return immediately if @entity is NULL.
 */
void media_entity_put(struct media_entity *entity);

/**
 * media_entity_graph_walk_start - Start walking the media graph at a given entity
 * @graph: Media graph structure that will be used to walk the graph
 * @entity: Starting entity
 *
 * Before using this function, media_entity_graph_walk_init() must be
 * used to allocate resources used for walking the graph. This
 * function initializes the graph traversal structure to walk the
 * entities graph starting at the given entity. The traversal
 * structure must not be modified by the caller during graph
 * traversal. After the graph walk, the resources must be released
 * using media_entity_graph_walk_cleanup().
 */
void media_entity_graph_walk_start(struct media_entity_graph *graph,
				   struct media_entity *entity);

/**
 * media_entity_graph_walk_next - Get the next entity in the graph
 * @graph: Media graph structure
 *
 * Perform a depth-first traversal of the given media entities graph.
 *
 * The graph structure must have been previously initialized with a call to
 * media_entity_graph_walk_start().
 *
 * Return the next entity in the graph or NULL if the whole graph have been
 * traversed.
 */
struct media_entity *
media_entity_graph_walk_next(struct media_entity_graph *graph);

/**
 * media_entity_pipeline_start - Mark a pipeline as streaming
 * @entity: Starting entity
 * @pipe: Media pipeline to be assigned to all entities in the pipeline.
 *
 * Mark all entities connected to a given entity through enabled links, either
 * directly or indirectly, as streaming. The given pipeline object is assigned to
 * every entity in the pipeline and stored in the media_entity pipe field.
 *
 * Calls to this function can be nested, in which case the same number of
 * media_entity_pipeline_stop() calls will be required to stop streaming. The
 * pipeline pointer must be identical for all nested calls to
 * media_entity_pipeline_start().
 */
__must_check int media_entity_pipeline_start(struct media_entity *entity,
					     struct media_pipeline *pipe);
/**
 * __media_entity_pipeline_start - Mark a pipeline as streaming
 *
 * @entity: Starting entity
 * @pipe: Media pipeline to be assigned to all entities in the pipeline.
 *
 * Note: This is the non-locking version of media_entity_pipeline_start()
 */
__must_check int __media_entity_pipeline_start(struct media_entity *entity,
					       struct media_pipeline *pipe);

/**
 * media_entity_pipeline_stop - Mark a pipeline as not streaming
 * @entity: Starting entity
 *
 * Mark all entities connected to a given entity through enabled links, either
 * directly or indirectly, as not streaming. The media_entity pipe field is
 * reset to NULL.
 *
 * If multiple calls to media_entity_pipeline_start() have been made, the same
 * number of calls to this function are required to mark the pipeline as not
 * streaming.
 */
void media_entity_pipeline_stop(struct media_entity *entity);

/**
 * __media_entity_pipeline_stop - Mark a pipeline as not streaming
 *
 * @entity: Starting entity
 *
 * Note: This is the non-locking version of media_entity_pipeline_stop()
 */
void __media_entity_pipeline_stop(struct media_entity *entity);

/**
 * media_devnode_create() - creates and initializes a device node interface
 *
 * @mdev:	pointer to struct &media_device
 * @type:	type of the interface, as given by MEDIA_INTF_T_* macros
 *		as defined in the uapi/media/media.h header.
 * @flags:	Interface flags as defined in uapi/media/media.h.
 * @major:	Device node major number.
 * @minor:	Device node minor number.
 *
 * Return: if succeeded, returns a pointer to the newly allocated
 *	&media_intf_devnode pointer.
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
struct media_link *

/**
 * media_create_intf_link() - creates a link between an entity and an interface
 *
 * @entity:	pointer to %media_entity
 * @intf:	pointer to %media_interface
 * @flags:	Link flags, as defined in include/uapi/linux/media.h.
 *
 *
 * Valid values for flags:
 * The %MEDIA_LNK_FL_ENABLED flag indicates that the interface is connected to
 *	the entity hardware. That's the default value for interfaces. An
 *	interface may be disabled if the hardware is busy due to the usage
 *	of some other interface that it is currently controlling the hardware.
 *	A typical example is an hybrid TV device that handle only one type of
 *	stream on a given time. So, when the digital TV is streaming,
 *	the V4L2 interfaces won't be enabled, as such device is not able to
 *	also stream analog TV or radio.
 *
 * Note:
 *
 * Before calling this function, media_devnode_create() should be called for
 * the interface and media_device_register_entity() should be called for the
 * interface that will be part of the link.
 */
__must_check media_create_intf_link(struct media_entity *entity,
				    struct media_interface *intf,
				    u32 flags);
/**
 * __media_remove_intf_link() - remove a single interface link
 *
 * @link:	pointer to &media_link.
 *
 * Note: this is an unlocked version of media_remove_intf_link()
 */
void __media_remove_intf_link(struct media_link *link);

/**
 * media_remove_intf_link() - remove a single interface link
 *
 * @link:	pointer to &media_link.
 *
 * Note: prefer to use this one, instead of __media_remove_intf_link()
 */
void media_remove_intf_link(struct media_link *link);

/**
 * __media_remove_intf_links() - remove all links associated with an interface
 *
 * @intf:	pointer to &media_interface
 *
 * Note: this is an unlocked version of media_remove_intf_links().
 */
void __media_remove_intf_links(struct media_interface *intf);

/**
 * media_remove_intf_links() - remove all links associated with an interface
 *
 * @intf:	pointer to &media_interface
 *
 * Notes:
 *
 * this is called automatically when an entity is unregistered via
 * media_device_register_entity() and by media_devnode_remove().
 *
 * Prefer to use this one, instead of __media_remove_intf_links().
 */
void media_remove_intf_links(struct media_interface *intf);

#define media_entity_call(entity, operation, args...)			\
	(((entity)->ops && (entity)->ops->operation) ?			\
	 (entity)->ops->operation((entity) , ##args) : -ENOIOCTLCMD)

#endif
