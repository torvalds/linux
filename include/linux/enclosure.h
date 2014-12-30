/*
 * Enclosure Services
 *
 * Copyright (C) 2008 James Bottomley <James.Bottomley@HansenPartnership.com>
 *
**-----------------------------------------------------------------------------
**
**  This program is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License
**  version 2 as published by the Free Software Foundation.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
*/
#ifndef _LINUX_ENCLOSURE_H_
#define _LINUX_ENCLOSURE_H_

#include <linux/device.h>
#include <linux/list.h>

/* A few generic types ... taken from ses-2 */
enum enclosure_component_type {
	ENCLOSURE_COMPONENT_DEVICE = 0x01,
	ENCLOSURE_COMPONENT_ARRAY_DEVICE = 0x17,
};

/* ses-2 common element status */
enum enclosure_status {
	ENCLOSURE_STATUS_UNSUPPORTED = 0,
	ENCLOSURE_STATUS_OK,
	ENCLOSURE_STATUS_CRITICAL,
	ENCLOSURE_STATUS_NON_CRITICAL,
	ENCLOSURE_STATUS_UNRECOVERABLE,
	ENCLOSURE_STATUS_NOT_INSTALLED,
	ENCLOSURE_STATUS_UNKNOWN,
	ENCLOSURE_STATUS_UNAVAILABLE,
	/* last element for counting purposes */
	ENCLOSURE_STATUS_MAX
};

/* SFF-8485 activity light settings */
enum enclosure_component_setting {
	ENCLOSURE_SETTING_DISABLED = 0,
	ENCLOSURE_SETTING_ENABLED = 1,
	ENCLOSURE_SETTING_BLINK_A_ON_OFF = 2,
	ENCLOSURE_SETTING_BLINK_A_OFF_ON = 3,
	ENCLOSURE_SETTING_BLINK_B_ON_OFF = 6,
	ENCLOSURE_SETTING_BLINK_B_OFF_ON = 7,
};

struct enclosure_device;
struct enclosure_component;
struct enclosure_component_callbacks {
	void (*get_status)(struct enclosure_device *,
			     struct enclosure_component *);
	int (*set_status)(struct enclosure_device *,
			  struct enclosure_component *,
			  enum enclosure_status);
	void (*get_fault)(struct enclosure_device *,
			  struct enclosure_component *);
	int (*set_fault)(struct enclosure_device *,
			 struct enclosure_component *,
			 enum enclosure_component_setting);
	void (*get_active)(struct enclosure_device *,
			   struct enclosure_component *);
	int (*set_active)(struct enclosure_device *,
			  struct enclosure_component *,
			  enum enclosure_component_setting);
	void (*get_locate)(struct enclosure_device *,
			   struct enclosure_component *);
	int (*set_locate)(struct enclosure_device *,
			  struct enclosure_component *,
			  enum enclosure_component_setting);
};


struct enclosure_component {
	void *scratch;
	struct device cdev;
	struct device *dev;
	enum enclosure_component_type type;
	int number;
	int fault;
	int active;
	int locate;
	enum enclosure_status status;
};

struct enclosure_device {
	void *scratch;
	struct list_head node;
	struct device edev;
	struct enclosure_component_callbacks *cb;
	int components;
	struct enclosure_component component[0];
};

static inline struct enclosure_device *
to_enclosure_device(struct device *dev)
{
	return container_of(dev, struct enclosure_device, edev);
}

static inline struct enclosure_component *
to_enclosure_component(struct device *dev)
{
	return container_of(dev, struct enclosure_component, cdev);
}

struct enclosure_device *
enclosure_register(struct device *, const char *, int,
		   struct enclosure_component_callbacks *);
void enclosure_unregister(struct enclosure_device *);
struct enclosure_component *
enclosure_component_alloc(struct enclosure_device *, unsigned int,
			  enum enclosure_component_type, const char *);
int enclosure_component_register(struct enclosure_component *);
int enclosure_add_device(struct enclosure_device *enclosure, int component,
			 struct device *dev);
int enclosure_remove_device(struct enclosure_device *, struct device *);
struct enclosure_device *enclosure_find(struct device *dev,
					struct enclosure_device *start);
int enclosure_for_each_device(int (*fn)(struct enclosure_device *, void *),
			      void *data);

#endif /* _LINUX_ENCLOSURE_H_ */
