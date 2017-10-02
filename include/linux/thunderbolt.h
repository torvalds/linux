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

#endif /* THUNDERBOLT_H_ */
