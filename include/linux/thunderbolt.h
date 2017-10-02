/*
 * Thunderbolt service API
 *
 * Copyright (C) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef THUNDERBOLT_H_
#define THUNDERBOLT_H_

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/mod_devicetable.h>
#include <linux/uuid.h>

enum tb_cfg_pkg_type {
	TB_CFG_PKG_READ = 1,
	TB_CFG_PKG_WRITE = 2,
	TB_CFG_PKG_ERROR = 3,
	TB_CFG_PKG_NOTIFY_ACK = 4,
	TB_CFG_PKG_EVENT = 5,
	TB_CFG_PKG_XDOMAIN_REQ = 6,
	TB_CFG_PKG_XDOMAIN_RESP = 7,
	TB_CFG_PKG_OVERRIDE = 8,
	TB_CFG_PKG_RESET = 9,
	TB_CFG_PKG_ICM_EVENT = 10,
	TB_CFG_PKG_ICM_CMD = 11,
	TB_CFG_PKG_ICM_RESP = 12,
	TB_CFG_PKG_PREPARE_TO_SLEEP = 13,
};

/**
 * enum tb_security_level - Thunderbolt security level
 * @TB_SECURITY_NONE: No security, legacy mode
 * @TB_SECURITY_USER: User approval required at minimum
 * @TB_SECURITY_SECURE: One time saved key required at minimum
 * @TB_SECURITY_DPONLY: Only tunnel Display port (and USB)
 */
enum tb_security_level {
	TB_SECURITY_NONE,
	TB_SECURITY_USER,
	TB_SECURITY_SECURE,
	TB_SECURITY_DPONLY,
};

/**
 * struct tb - main thunderbolt bus structure
 * @dev: Domain device
 * @lock: Big lock. Must be held when accessing any struct
 *	  tb_switch / struct tb_port.
 * @nhi: Pointer to the NHI structure
 * @ctl: Control channel for this domain
 * @wq: Ordered workqueue for all domain specific work
 * @root_switch: Root switch of this domain
 * @cm_ops: Connection manager specific operations vector
 * @index: Linux assigned domain number
 * @security_level: Current security level
 * @privdata: Private connection manager specific data
 */
struct tb {
	struct device dev;
	struct mutex lock;
	struct tb_nhi *nhi;
	struct tb_ctl *ctl;
	struct workqueue_struct *wq;
	struct tb_switch *root_switch;
	const struct tb_cm_ops *cm_ops;
	int index;
	enum tb_security_level security_level;
	unsigned long privdata[0];
};

extern struct bus_type tb_bus_type;
extern struct device_type tb_service_type;
extern struct device_type tb_xdomain_type;

#define TB_LINKS_PER_PHY_PORT	2

static inline unsigned int tb_phy_port_from_link(unsigned int link)
{
	return (link - 1) / TB_LINKS_PER_PHY_PORT;
}

/**
 * struct tb_property_dir - XDomain property directory
 * @uuid: Directory UUID or %NULL if root directory
 * @properties: List of properties in this directory
 *
 * User needs to provide serialization if needed.
 */
struct tb_property_dir {
	const uuid_t *uuid;
	struct list_head properties;
};

enum tb_property_type {
	TB_PROPERTY_TYPE_UNKNOWN = 0x00,
	TB_PROPERTY_TYPE_DIRECTORY = 0x44,
	TB_PROPERTY_TYPE_DATA = 0x64,
	TB_PROPERTY_TYPE_TEXT = 0x74,
	TB_PROPERTY_TYPE_VALUE = 0x76,
};

#define TB_PROPERTY_KEY_SIZE	8

/**
 * struct tb_property - XDomain property
 * @list: Used to link properties together in a directory
 * @key: Key for the property (always terminated).
 * @type: Type of the property
 * @length: Length of the property data in dwords
 * @value: Property value
 *
 * Users use @type to determine which field in @value is filled.
 */
struct tb_property {
	struct list_head list;
	char key[TB_PROPERTY_KEY_SIZE + 1];
	enum tb_property_type type;
	size_t length;
	union {
		struct tb_property_dir *dir;
		u8 *data;
		char *text;
		u32 immediate;
	} value;
};

struct tb_property_dir *tb_property_parse_dir(const u32 *block,
					      size_t block_len);
ssize_t tb_property_format_dir(const struct tb_property_dir *dir, u32 *block,
			       size_t block_len);
struct tb_property_dir *tb_property_create_dir(const uuid_t *uuid);
void tb_property_free_dir(struct tb_property_dir *dir);
int tb_property_add_immediate(struct tb_property_dir *parent, const char *key,
			      u32 value);
int tb_property_add_data(struct tb_property_dir *parent, const char *key,
			 const void *buf, size_t buflen);
int tb_property_add_text(struct tb_property_dir *parent, const char *key,
			 const char *text);
int tb_property_add_dir(struct tb_property_dir *parent, const char *key,
			struct tb_property_dir *dir);
void tb_property_remove(struct tb_property *tb_property);
struct tb_property *tb_property_find(struct tb_property_dir *dir,
			const char *key, enum tb_property_type type);
struct tb_property *tb_property_get_next(struct tb_property_dir *dir,
					 struct tb_property *prev);

#define tb_property_for_each(dir, property)			\
	for (property = tb_property_get_next(dir, NULL);	\
	     property;						\
	     property = tb_property_get_next(dir, property))

int tb_register_property_dir(const char *key, struct tb_property_dir *dir);
void tb_unregister_property_dir(const char *key, struct tb_property_dir *dir);

/**
 * struct tb_xdomain - Cross-domain (XDomain) connection
 * @dev: XDomain device
 * @tb: Pointer to the domain
 * @remote_uuid: UUID of the remote domain (host)
 * @local_uuid: Cached local UUID
 * @route: Route string the other domain can be reached
 * @vendor: Vendor ID of the remote domain
 * @device: Device ID of the demote domain
 * @lock: Lock to serialize access to the following fields of this structure
 * @vendor_name: Name of the vendor (or %NULL if not known)
 * @device_name: Name of the device (or %NULL if not known)
 * @is_unplugged: The XDomain is unplugged
 * @resume: The XDomain is being resumed
 * @transmit_path: HopID which the remote end expects us to transmit
 * @transmit_ring: Local ring (hop) where outgoing packets are pushed
 * @receive_path: HopID which we expect the remote end to transmit
 * @receive_ring: Local ring (hop) where incoming packets arrive
 * @service_ids: Used to generate IDs for the services
 * @properties: Properties exported by the remote domain
 * @property_block_gen: Generation of @properties
 * @properties_lock: Lock protecting @properties.
 * @get_properties_work: Work used to get remote domain properties
 * @properties_retries: Number of times left to read properties
 * @properties_changed_work: Work used to notify the remote domain that
 *			     our properties have changed
 * @properties_changed_retries: Number of times left to send properties
 *				changed notification
 * @link: Root switch link the remote domain is connected (ICM only)
 * @depth: Depth in the chain the remote domain is connected (ICM only)
 *
 * This structure represents connection across two domains (hosts).
 * Each XDomain contains zero or more services which are exposed as
 * &struct tb_service objects.
 *
 * Service drivers may access this structure if they need to enumerate
 * non-standard properties but they need hold @lock when doing so
 * because properties can be changed asynchronously in response to
 * changes in the remote domain.
 */
struct tb_xdomain {
	struct device dev;
	struct tb *tb;
	uuid_t *remote_uuid;
	const uuid_t *local_uuid;
	u64 route;
	u16 vendor;
	u16 device;
	struct mutex lock;
	const char *vendor_name;
	const char *device_name;
	bool is_unplugged;
	bool resume;
	u16 transmit_path;
	u16 transmit_ring;
	u16 receive_path;
	u16 receive_ring;
	struct ida service_ids;
	struct tb_property_dir *properties;
	u32 property_block_gen;
	struct delayed_work get_properties_work;
	int properties_retries;
	struct delayed_work properties_changed_work;
	int properties_changed_retries;
	u8 link;
	u8 depth;
};

int tb_xdomain_enable_paths(struct tb_xdomain *xd, u16 transmit_path,
			    u16 transmit_ring, u16 receive_path,
			    u16 receive_ring);
int tb_xdomain_disable_paths(struct tb_xdomain *xd);
struct tb_xdomain *tb_xdomain_find_by_uuid(struct tb *tb, const uuid_t *uuid);

static inline struct tb_xdomain *
tb_xdomain_find_by_uuid_locked(struct tb *tb, const uuid_t *uuid)
{
	struct tb_xdomain *xd;

	mutex_lock(&tb->lock);
	xd = tb_xdomain_find_by_uuid(tb, uuid);
	mutex_unlock(&tb->lock);

	return xd;
}

static inline struct tb_xdomain *tb_xdomain_get(struct tb_xdomain *xd)
{
	if (xd)
		get_device(&xd->dev);
	return xd;
}

static inline void tb_xdomain_put(struct tb_xdomain *xd)
{
	if (xd)
		put_device(&xd->dev);
}

static inline bool tb_is_xdomain(const struct device *dev)
{
	return dev->type == &tb_xdomain_type;
}

static inline struct tb_xdomain *tb_to_xdomain(struct device *dev)
{
	if (tb_is_xdomain(dev))
		return container_of(dev, struct tb_xdomain, dev);
	return NULL;
}

int tb_xdomain_response(struct tb_xdomain *xd, const void *response,
			size_t size, enum tb_cfg_pkg_type type);
int tb_xdomain_request(struct tb_xdomain *xd, const void *request,
		       size_t request_size, enum tb_cfg_pkg_type request_type,
		       void *response, size_t response_size,
		       enum tb_cfg_pkg_type response_type,
		       unsigned int timeout_msec);

/**
 * tb_protocol_handler - Protocol specific handler
 * @uuid: XDomain messages with this UUID are dispatched to this handler
 * @callback: Callback called with the XDomain message. Returning %1
 *	      here tells the XDomain core that the message was handled
 *	      by this handler and should not be forwared to other
 *	      handlers.
 * @data: Data passed with the callback
 * @list: Handlers are linked using this
 *
 * Thunderbolt services can hook into incoming XDomain requests by
 * registering protocol handler. Only limitation is that the XDomain
 * discovery protocol UUID cannot be registered since it is handled by
 * the core XDomain code.
 *
 * The @callback must check that the message is really directed to the
 * service the driver implements.
 */
struct tb_protocol_handler {
	const uuid_t *uuid;
	int (*callback)(const void *buf, size_t size, void *data);
	void *data;
	struct list_head list;
};

int tb_register_protocol_handler(struct tb_protocol_handler *handler);
void tb_unregister_protocol_handler(struct tb_protocol_handler *handler);

/**
 * struct tb_service - Thunderbolt service
 * @dev: XDomain device
 * @id: ID of the service (shown in sysfs)
 * @key: Protocol key from the properties directory
 * @prtcid: Protocol ID from the properties directory
 * @prtcvers: Protocol version from the properties directory
 * @prtcrevs: Protocol software revision from the properties directory
 * @prtcstns: Protocol settings mask from the properties directory
 *
 * Each domain exposes set of services it supports as collection of
 * properties. For each service there will be one corresponding
 * &struct tb_service. Service drivers are bound to these.
 */
struct tb_service {
	struct device dev;
	int id;
	const char *key;
	u32 prtcid;
	u32 prtcvers;
	u32 prtcrevs;
	u32 prtcstns;
};

static inline struct tb_service *tb_service_get(struct tb_service *svc)
{
	if (svc)
		get_device(&svc->dev);
	return svc;
}

static inline void tb_service_put(struct tb_service *svc)
{
	if (svc)
		put_device(&svc->dev);
}

static inline bool tb_is_service(const struct device *dev)
{
	return dev->type == &tb_service_type;
}

static inline struct tb_service *tb_to_service(struct device *dev)
{
	if (tb_is_service(dev))
		return container_of(dev, struct tb_service, dev);
	return NULL;
}

/**
 * tb_service_driver - Thunderbolt service driver
 * @driver: Driver structure
 * @probe: Called when the driver is probed
 * @remove: Called when the driver is removed (optional)
 * @shutdown: Called at shutdown time to stop the service (optional)
 * @id_table: Table of service identifiers the driver supports
 */
struct tb_service_driver {
	struct device_driver driver;
	int (*probe)(struct tb_service *svc, const struct tb_service_id *id);
	void (*remove)(struct tb_service *svc);
	void (*shutdown)(struct tb_service *svc);
	const struct tb_service_id *id_table;
};

#define TB_SERVICE(key, id)				\
	.match_flags = TBSVC_MATCH_PROTOCOL_KEY |	\
		       TBSVC_MATCH_PROTOCOL_ID,		\
	.protocol_key = (key),				\
	.protocol_id = (id)

int tb_register_service_driver(struct tb_service_driver *drv);
void tb_unregister_service_driver(struct tb_service_driver *drv);

static inline void *tb_service_get_drvdata(const struct tb_service *svc)
{
	return dev_get_drvdata(&svc->dev);
}

static inline void tb_service_set_drvdata(struct tb_service *svc, void *data)
{
	dev_set_drvdata(&svc->dev, data);
}

static inline struct tb_xdomain *tb_service_parent(struct tb_service *svc)
{
	return tb_to_xdomain(svc->dev.parent);
}

#endif /* THUNDERBOLT_H_ */
