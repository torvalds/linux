/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Copyright (C) 2017-2020 Mellanox Technologies Ltd.
 */

#ifndef __LINUX_PLATFORM_DATA_MLXREG_H
#define __LINUX_PLATFORM_DATA_MLXREG_H

#define MLXREG_CORE_LABEL_MAX_SIZE	32
#define MLXREG_CORE_WD_FEATURE_NOWAYOUT		BIT(0)
#define MLXREG_CORE_WD_FEATURE_START_AT_BOOT	BIT(1)

/**
 * enum mlxreg_wdt_type - type of HW watchdog
 *
 * TYPE1 HW watchdog implementation exist in old systems.
 * All new systems have TYPE2 HW watchdog.
 * TYPE3 HW watchdog can exist on all systems with new CPLD.
 * TYPE3 is selected by WD capability bit.
 */
enum mlxreg_wdt_type {
	MLX_WDT_TYPE1,
	MLX_WDT_TYPE2,
	MLX_WDT_TYPE3,
};

/**
 * enum mlxreg_hotplug_kind - kind of hotplug entry
 *
 * @MLXREG_HOTPLUG_DEVICE_NA: do not care;
 * @MLXREG_HOTPLUG_LC_PRESENT: entry for line card presence in/out events;
 * @MLXREG_HOTPLUG_LC_VERIFIED: entry for line card verification status events
 *				coming after line card security signature validation;
 * @MLXREG_HOTPLUG_LC_POWERED: entry for line card power on/off events;
 * @MLXREG_HOTPLUG_LC_SYNCED: entry for line card synchronization events, coming
 *			      after hardware-firmware synchronization handshake;
 * @MLXREG_HOTPLUG_LC_READY: entry for line card ready events, indicating line card
			     PHYs ready / unready state;
 * @MLXREG_HOTPLUG_LC_ACTIVE: entry for line card active events, indicating firmware
 *			      availability / unavailability for the ports on line card;
 * @MLXREG_HOTPLUG_LC_THERMAL: entry for line card thermal shutdown events, positive
 *			       event indicates that system should power off the line
 *			       card for which this event has been received;
 */
enum mlxreg_hotplug_kind {
	MLXREG_HOTPLUG_DEVICE_NA = 0,
	MLXREG_HOTPLUG_LC_PRESENT = 1,
	MLXREG_HOTPLUG_LC_VERIFIED = 2,
	MLXREG_HOTPLUG_LC_POWERED = 3,
	MLXREG_HOTPLUG_LC_SYNCED = 4,
	MLXREG_HOTPLUG_LC_READY = 5,
	MLXREG_HOTPLUG_LC_ACTIVE = 6,
	MLXREG_HOTPLUG_LC_THERMAL = 7,
};

/**
 * enum mlxreg_hotplug_device_action - hotplug device action required for
 *				       driver's connectivity
 *
 * @MLXREG_HOTPLUG_DEVICE_DEFAULT_ACTION: probe device for 'on' event, remove
 *					  for 'off' event;
 * @MLXREG_HOTPLUG_DEVICE_PLATFORM_ACTION: probe platform device for 'on'
 *					   event, remove for 'off' event;
 * @MLXREG_HOTPLUG_DEVICE_NO_ACTION: no connectivity action is required;
 */
enum mlxreg_hotplug_device_action {
	MLXREG_HOTPLUG_DEVICE_DEFAULT_ACTION = 0,
	MLXREG_HOTPLUG_DEVICE_PLATFORM_ACTION = 1,
	MLXREG_HOTPLUG_DEVICE_NO_ACTION = 2,
};

/**
 * struct mlxreg_core_hotplug_notifier - hotplug notifier block:
 *
 * @identity: notifier identity name;
 * @handle: user handle to be passed by user handler function;
 * @user_handler: user handler function associated with the event;
 */
struct mlxreg_core_hotplug_notifier {
	char identity[MLXREG_CORE_LABEL_MAX_SIZE];
	void *handle;
	int (*user_handler)(void *handle, enum mlxreg_hotplug_kind kind, u8 action);
};

/**
 * struct mlxreg_hotplug_device - I2C device data:
 *
 * @adapter: I2C device adapter;
 * @client: I2C device client;
 * @brdinfo: device board information;
 * @nr: I2C device adapter number, to which device is to be attached;
 * @pdev: platform device, if device is instantiated as a platform device;
 * @action: action to be performed upon event receiving;
 * @handle: user handle to be passed by user handler function;
 * @user_handler: user handler function associated with the event;
 * @notifier: pointer to event notifier block;
 *
 * Structure represents I2C hotplug device static data (board topology) and
 * dynamic data (related kernel objects handles).
 */
struct mlxreg_hotplug_device {
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info *brdinfo;
	int nr;
	struct platform_device *pdev;
	enum mlxreg_hotplug_device_action action;
	void *handle;
	int (*user_handler)(void *handle, enum mlxreg_hotplug_kind kind, u8 action);
	struct mlxreg_core_hotplug_notifier *notifier;
};

/**
 * struct mlxreg_core_data - attributes control data:
 *
 * @label: attribute label;
 * @reg: attribute register;
 * @mask: attribute access mask;
 * @bit: attribute effective bit;
 * @capability: attribute capability register;
 * @reg_prsnt: attribute presence register;
 * @reg_sync: attribute synch register;
 * @reg_pwr: attribute power register;
 * @reg_ena: attribute enable register;
 * @mode: access mode;
 * @np - pointer to node platform associated with attribute;
 * @hpdev - hotplug device data;
 * @notifier: pointer to event notifier block;
 * @health_cntr: dynamic device health indication counter;
 * @attached: true if device has been attached after good health indication;
 * @regnum: number of registers occupied by multi-register attribute;
 * @slot: slot number, at which device is located;
 * @secured: if set indicates that entry access is secured;
 */
struct mlxreg_core_data {
	char label[MLXREG_CORE_LABEL_MAX_SIZE];
	u32 reg;
	u32 mask;
	u32 bit;
	u32 capability;
	u32 reg_prsnt;
	u32 reg_sync;
	u32 reg_pwr;
	u32 reg_ena;
	umode_t	mode;
	struct device_node *np;
	struct mlxreg_hotplug_device hpdev;
	struct mlxreg_core_hotplug_notifier *notifier;
	u32 health_cntr;
	bool attached;
	u8 regnum;
	u8 slot;
	u8 secured;
};

/**
 * struct mlxreg_core_item - same type components controlled by the driver:
 *
 * @data: component data;
 * @kind: kind of hotplug attribute;
 * @aggr_mask: group aggregation mask;
 * @reg: group interrupt status register;
 * @mask: group interrupt mask;
 * @capability: group capability register;
 * @cache: last status value for elements fro the same group;
 * @count: number of available elements in the group;
 * @ind: element's index inside the group;
 * @inversed: if 0: 0 for signal status is OK, if 1 - 1 is OK;
 * @health: true if device has health indication, false in other case;
 */
struct mlxreg_core_item {
	struct mlxreg_core_data *data;
	enum mlxreg_hotplug_kind kind;
	u32 aggr_mask;
	u32 reg;
	u32 mask;
	u32 capability;
	u32 cache;
	u8 count;
	u8 ind;
	u8 inversed;
	u8 health;
};

/**
 * struct mlxreg_core_platform_data - platform data:
 *
 * @data: instance private data;
 * @regmap: register map of parent device;
 * @counter: number of instances;
 * @features: supported features of device;
 * @version: implementation version;
 * @identity: device identity name;
 * @capability: device capability register;
 */
struct mlxreg_core_platform_data {
	struct mlxreg_core_data *data;
	void *regmap;
	int counter;
	u32 features;
	u32 version;
	char identity[MLXREG_CORE_LABEL_MAX_SIZE];
	u32 capability;
};

/**
 * struct mlxreg_core_hotplug_platform_data - hotplug platform data:
 *
 * @items: same type components with the hotplug capability;
 * @irq: platform interrupt number;
 * @regmap: register map of parent device;
 * @count: number of the components with the hotplug capability;
 * @cell: location of top aggregation interrupt register;
 * @mask: top aggregation interrupt common mask;
 * @cell_low: location of low aggregation interrupt register;
 * @mask_low: low aggregation interrupt common mask;
 * @deferred_nr: I2C adapter number must be exist prior probing execution;
 * @shift_nr: I2C adapter numbers must be incremented by this value;
 * @addr: mapped resource address;
 * @handle: handle to be passed by callback;
 * @completion_notify: callback to notify when platform driver probing is done;
 */
struct mlxreg_core_hotplug_platform_data {
	struct mlxreg_core_item *items;
	int irq;
	void *regmap;
	int count;
	u32 cell;
	u32 mask;
	u32 cell_low;
	u32 mask_low;
	int deferred_nr;
	int shift_nr;
	void __iomem *addr;
	void *handle;
	int (*completion_notify)(void *handle, int id);
};

#endif /* __LINUX_PLATFORM_DATA_MLXREG_H */
