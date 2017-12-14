// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SOUNDWIRE_H
#define __SOUNDWIRE_H

struct sdw_bus;
struct sdw_slave;

/* SDW spec defines and enums, as defined by MIPI 1.1. Spec */

/* SDW Broadcast Device Number */
#define SDW_BROADCAST_DEV_NUM		15

/* SDW Enumeration Device Number */
#define SDW_ENUM_DEV_NUM		0

/* SDW Group Device Numbers */
#define SDW_GROUP12_DEV_NUM		12
#define SDW_GROUP13_DEV_NUM		13

/* SDW Master Device Number, not supported yet */
#define SDW_MASTER_DEV_NUM		14

#define SDW_MAX_DEVICES			11

/**
 * enum sdw_slave_status - Slave status
 * @SDW_SLAVE_UNATTACHED: Slave is not attached with the bus.
 * @SDW_SLAVE_ATTACHED: Slave is attached with bus.
 * @SDW_SLAVE_ALERT: Some alert condition on the Slave
 * @SDW_SLAVE_RESERVED: Reserved for future use
 */
enum sdw_slave_status {
	SDW_SLAVE_UNATTACHED = 0,
	SDW_SLAVE_ATTACHED = 1,
	SDW_SLAVE_ALERT = 2,
	SDW_SLAVE_RESERVED = 3,
};

/*
 * SDW Slave Structures and APIs
 */

/**
 * struct sdw_slave_id - Slave ID
 * @mfg_id: MIPI Manufacturer ID
 * @part_id: Device Part ID
 * @class_id: MIPI Class ID, unused now.
 * Currently a placeholder in MIPI SoundWire Spec
 * @unique_id: Device unique ID
 * @sdw_version: SDW version implemented
 *
 * The order of the IDs here does not follow the DisCo spec definitions
 */
struct sdw_slave_id {
	__u16 mfg_id;
	__u16 part_id;
	__u8 class_id;
	__u8 unique_id:4;
	__u8 sdw_version:4;
};

/**
 * struct sdw_slave - SoundWire Slave
 * @id: MIPI device ID
 * @dev: Linux device
 * @status: Status reported by the Slave
 * @bus: Bus handle
 * @node: node for bus list
 * @dev_num: Device Number assigned by Bus
 */
struct sdw_slave {
	struct sdw_slave_id id;
	struct device dev;
	enum sdw_slave_status status;
	struct sdw_bus *bus;
	struct list_head node;
	u16 dev_num;
};

#define dev_to_sdw_dev(_dev) container_of(_dev, struct sdw_slave, dev)

struct sdw_driver {
	const char *name;

	int (*probe)(struct sdw_slave *sdw,
			const struct sdw_device_id *id);
	int (*remove)(struct sdw_slave *sdw);
	void (*shutdown)(struct sdw_slave *sdw);

	const struct sdw_device_id *id_table;
	const struct sdw_slave_ops *ops;

	struct device_driver driver;
};

#define SDW_SLAVE_ENTRY(_mfg_id, _part_id, _drv_data) \
	{ .mfg_id = (_mfg_id), .part_id = (_part_id), \
	  .driver_data = (unsigned long)(_drv_data) }

/*
 * SDW master structures and APIs
 */

/**
 * struct sdw_bus - SoundWire bus
 * @dev: Master linux device
 * @link_id: Link id number, can be 0 to N, unique for each Master
 * @slaves: list of Slaves on this bus
 * @assigned: Bitmap for Slave device numbers.
 * Bit set implies used number, bit clear implies unused number.
 * @bus_lock: bus lock
 */
struct sdw_bus {
	struct device *dev;
	unsigned int link_id;
	struct list_head slaves;
	DECLARE_BITMAP(assigned, SDW_MAX_DEVICES);
	struct mutex bus_lock;
};

int sdw_add_bus_master(struct sdw_bus *bus);
void sdw_delete_bus_master(struct sdw_bus *bus);

#endif /* __SOUNDWIRE_H */
