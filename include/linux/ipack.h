/*
 * Industry-pack bus.
 *
 * Copyright (C) 2011-2012 CERN (www.cern.ch)
 * Author: Samuel Iglesias Gonsalvez <siglesias@igalia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/mod_devicetable.h>
#include <linux/device.h>
#include <linux/interrupt.h>

#define IPACK_IDPROM_OFFSET_I			0x01
#define IPACK_IDPROM_OFFSET_P			0x03
#define IPACK_IDPROM_OFFSET_A			0x05
#define IPACK_IDPROM_OFFSET_C			0x07
#define IPACK_IDPROM_OFFSET_MANUFACTURER_ID	0x09
#define IPACK_IDPROM_OFFSET_MODEL		0x0B
#define IPACK_IDPROM_OFFSET_REVISION		0x0D
#define IPACK_IDPROM_OFFSET_RESERVED		0x0F
#define IPACK_IDPROM_OFFSET_DRIVER_ID_L		0x11
#define IPACK_IDPROM_OFFSET_DRIVER_ID_H		0x13
#define IPACK_IDPROM_OFFSET_NUM_BYTES		0x15
#define IPACK_IDPROM_OFFSET_CRC			0x17

/*
 * IndustryPack Fromat, Vendor and Device IDs.
 */

/* ID section format versions */
#define IPACK_ID_VERSION_INVALID	0x00
#define IPACK_ID_VERSION_1		0x01
#define IPACK_ID_VERSION_2		0x02

/* Vendors and devices. Sort key: vendor first, device next. */
#define IPACK1_VENDOR_ID_RESERVED1	0x00
#define IPACK1_VENDOR_ID_RESERVED2	0xFF
#define IPACK1_VENDOR_ID_UNREGISTRED01	0x01
#define IPACK1_VENDOR_ID_UNREGISTRED02	0x02
#define IPACK1_VENDOR_ID_UNREGISTRED03	0x03
#define IPACK1_VENDOR_ID_UNREGISTRED04	0x04
#define IPACK1_VENDOR_ID_UNREGISTRED05	0x05
#define IPACK1_VENDOR_ID_UNREGISTRED06	0x06
#define IPACK1_VENDOR_ID_UNREGISTRED07	0x07
#define IPACK1_VENDOR_ID_UNREGISTRED08	0x08
#define IPACK1_VENDOR_ID_UNREGISTRED09	0x09
#define IPACK1_VENDOR_ID_UNREGISTRED10	0x0A
#define IPACK1_VENDOR_ID_UNREGISTRED11	0x0B
#define IPACK1_VENDOR_ID_UNREGISTRED12	0x0C
#define IPACK1_VENDOR_ID_UNREGISTRED13	0x0D
#define IPACK1_VENDOR_ID_UNREGISTRED14	0x0E
#define IPACK1_VENDOR_ID_UNREGISTRED15	0x0F

#define IPACK1_VENDOR_ID_SBS            0xF0
#define IPACK1_DEVICE_ID_SBS_OCTAL_232  0x22
#define IPACK1_DEVICE_ID_SBS_OCTAL_422  0x2A
#define IPACK1_DEVICE_ID_SBS_OCTAL_485  0x48

struct ipack_bus_ops;
struct ipack_driver;

enum ipack_space {
	IPACK_IO_SPACE    = 0,
	IPACK_ID_SPACE,
	IPACK_INT_SPACE,
	IPACK_MEM8_SPACE,
	IPACK_MEM16_SPACE,
	/* Dummy for counting the number of entries.  Must remain the last
	 * entry */
	IPACK_SPACE_COUNT,
};

/**
 */
struct ipack_region {
	phys_addr_t start;
	size_t      size;
};

/**
 *	struct ipack_device
 *
 *	@slot: Slot where the device is plugged in the carrier board
 *	@bus: ipack_bus_device where the device is plugged to.
 *	@id_space: Virtual address to ID space.
 *	@io_space: Virtual address to IO space.
 *	@mem_space: Virtual address to MEM space.
 *	@dev: device in kernel representation.
 *
 * Warning: Direct access to mapped memory is possible but the endianness
 * is not the same with PCI carrier or VME carrier. The endianness is managed
 * by the carrier board throught bus->ops.
 */
struct ipack_device {
	unsigned int slot;
	struct ipack_bus_device *bus;
	struct device dev;
	void (*release) (struct ipack_device *dev);
	struct ipack_region      region[IPACK_SPACE_COUNT];
	u8                      *id;
	size_t			 id_avail;
	u32			 id_vendor;
	u32			 id_device;
	u8			 id_format;
	unsigned int		 id_crc_correct:1;
	unsigned int		 speed_8mhz:1;
	unsigned int		 speed_32mhz:1;
};

/**
 * struct ipack_driver_ops -- Callbacks to IPack device driver
 *
 * @probe:  Probe function
 * @remove: Prepare imminent removal of the device.  Services provided by the
 *          device should be revoked.
 */

struct ipack_driver_ops {
	int (*probe) (struct ipack_device *dev);
	void (*remove) (struct ipack_device *dev);
};

/**
 * struct ipack_driver -- Specific data to each ipack device driver
 *
 * @driver: Device driver kernel representation
 * @ops:    Callbacks provided by the IPack device driver
 */
struct ipack_driver {
	struct device_driver driver;
	const struct ipack_device_id *id_table;
	const struct ipack_driver_ops *ops;
};

/**
 *	struct ipack_bus_ops - available operations on a bridge module
 *
 *	@map_space: map IP address space
 *	@unmap_space: unmap IP address space
 *	@request_irq: request IRQ
 *	@free_irq: free IRQ
 *	@get_clockrate: Returns the clockrate the carrier is currently
 *		communicating with the device at.
 *	@set_clockrate: Sets the clock-rate for carrier / module communication.
 *		Should return -EINVAL if the requested speed is not supported.
 *	@get_error: Returns the error state for the slot the device is attached
 *		to.
 *	@get_timeout: Returns 1 if the communication with the device has
 *		previously timed out.
 *	@reset_timeout: Resets the state returned by get_timeout.
 */
struct ipack_bus_ops {
	int (*request_irq) (struct ipack_device *dev,
			    irqreturn_t (*handler)(void *), void *arg);
	int (*free_irq) (struct ipack_device *dev);
	int (*get_clockrate) (struct ipack_device *dev);
	int (*set_clockrate) (struct ipack_device *dev, int mherz);
	int (*get_error) (struct ipack_device *dev);
	int (*get_timeout) (struct ipack_device *dev);
	int (*reset_timeout) (struct ipack_device *dev);
};

/**
 *	struct ipack_bus_device
 *
 *	@dev: pointer to carrier device
 *	@slots: number of slots available
 *	@bus_nr: ipack bus number
 *	@ops: bus operations for the mezzanine drivers
 */
struct ipack_bus_device {
	struct device *parent;
	int slots;
	int bus_nr;
	const struct ipack_bus_ops *ops;
};

/**
 *	ipack_bus_register -- register a new ipack bus
 *
 * @parent: pointer to the parent device, if any.
 * @slots: number of slots available in the bus device.
 * @ops: bus operations for the mezzanine drivers.
 *
 * The carrier board device should call this function to register itself as
 * available bus device in ipack.
 */
struct ipack_bus_device *ipack_bus_register(struct device *parent, int slots,
					    const struct ipack_bus_ops *ops);

/**
 *	ipack_bus_unregister -- unregister an ipack bus
 */
int ipack_bus_unregister(struct ipack_bus_device *bus);

/**
 * ipack_driver_register -- Register a new ipack device driver
 *
 * Called by a ipack driver to register itself as a driver
 * that can manage ipack devices.
 */
int ipack_driver_register(struct ipack_driver *edrv, struct module *owner,
			  const char *name);
void ipack_driver_unregister(struct ipack_driver *edrv);

/**
 *	ipack_device_register -- register an IPack device with the kernel
 *	@dev: the new device to register.
 *
 *	Register a new IPack device ("module" in IndustryPack jargon). The call
 *	is done by the carrier driver.  The carrier should populate the fields
 *	bus and slot as well as the region array of @dev prior to calling this
 *	function.  The rest of the fields will be allocated and populated
 *	during registration.
 *
 *	Return zero on success or error code on failure.
 */
int ipack_device_register(struct ipack_device *dev);
void ipack_device_unregister(struct ipack_device *dev);

/**
 * DEFINE_IPACK_DEVICE_TABLE - macro used to describe a IndustryPack table
 * @_table: device table name
 *
 * This macro is used to create a struct ipack_device_id array (a device table)
 * in a generic manner.
 */
#define DEFINE_IPACK_DEVICE_TABLE(_table) \
	const struct ipack_device_id _table[]
/**
 * IPACK_DEVICE - macro used to describe a specific IndustryPack device
 * @_format: the format version (currently either 1 or 2, 8 bit value)
 * @vend:    the 8 or 24 bit IndustryPack Vendor ID
 * @dev:     the 8 or 16  bit IndustryPack Device ID
 *
 * This macro is used to create a struct ipack_device_id that matches a specific
 * device.
 */
#define IPACK_DEVICE(_format, vend, dev) \
	 .format = (_format), \
	 .vendor = (vend), \
	 .device = (dev)
