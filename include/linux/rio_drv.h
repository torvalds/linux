/*
 * RapidIO driver services
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef LINUX_RIO_DRV_H
#define LINUX_RIO_DRV_H

#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/rio.h>

extern int __rio_local_read_config_32(struct rio_mport *port, u32 offset,
				      u32 * data);
extern int __rio_local_write_config_32(struct rio_mport *port, u32 offset,
				       u32 data);
extern int __rio_local_read_config_16(struct rio_mport *port, u32 offset,
				      u16 * data);
extern int __rio_local_write_config_16(struct rio_mport *port, u32 offset,
				       u16 data);
extern int __rio_local_read_config_8(struct rio_mport *port, u32 offset,
				     u8 * data);
extern int __rio_local_write_config_8(struct rio_mport *port, u32 offset,
				      u8 data);

extern int rio_mport_read_config_32(struct rio_mport *port, u16 destid,
				    u8 hopcount, u32 offset, u32 * data);
extern int rio_mport_write_config_32(struct rio_mport *port, u16 destid,
				     u8 hopcount, u32 offset, u32 data);
extern int rio_mport_read_config_16(struct rio_mport *port, u16 destid,
				    u8 hopcount, u32 offset, u16 * data);
extern int rio_mport_write_config_16(struct rio_mport *port, u16 destid,
				     u8 hopcount, u32 offset, u16 data);
extern int rio_mport_read_config_8(struct rio_mport *port, u16 destid,
				   u8 hopcount, u32 offset, u8 * data);
extern int rio_mport_write_config_8(struct rio_mport *port, u16 destid,
				    u8 hopcount, u32 offset, u8 data);

/**
 * rio_local_read_config_32 - Read 32 bits from local configuration space
 * @port: Master port
 * @offset: Offset into local configuration space
 * @data: Pointer to read data into
 *
 * Reads 32 bits of data from the specified offset within the local
 * device's configuration space.
 */
static inline int rio_local_read_config_32(struct rio_mport *port, u32 offset,
					   u32 * data)
{
	return __rio_local_read_config_32(port, offset, data);
}

/**
 * rio_local_write_config_32 - Write 32 bits to local configuration space
 * @port: Master port
 * @offset: Offset into local configuration space
 * @data: Data to be written
 *
 * Writes 32 bits of data to the specified offset within the local
 * device's configuration space.
 */
static inline int rio_local_write_config_32(struct rio_mport *port, u32 offset,
					    u32 data)
{
	return __rio_local_write_config_32(port, offset, data);
}

/**
 * rio_local_read_config_16 - Read 16 bits from local configuration space
 * @port: Master port
 * @offset: Offset into local configuration space
 * @data: Pointer to read data into
 *
 * Reads 16 bits of data from the specified offset within the local
 * device's configuration space.
 */
static inline int rio_local_read_config_16(struct rio_mport *port, u32 offset,
					   u16 * data)
{
	return __rio_local_read_config_16(port, offset, data);
}

/**
 * rio_local_write_config_16 - Write 16 bits to local configuration space
 * @port: Master port
 * @offset: Offset into local configuration space
 * @data: Data to be written
 *
 * Writes 16 bits of data to the specified offset within the local
 * device's configuration space.
 */

static inline int rio_local_write_config_16(struct rio_mport *port, u32 offset,
					    u16 data)
{
	return __rio_local_write_config_16(port, offset, data);
}

/**
 * rio_local_read_config_8 - Read 8 bits from local configuration space
 * @port: Master port
 * @offset: Offset into local configuration space
 * @data: Pointer to read data into
 *
 * Reads 8 bits of data from the specified offset within the local
 * device's configuration space.
 */
static inline int rio_local_read_config_8(struct rio_mport *port, u32 offset,
					  u8 * data)
{
	return __rio_local_read_config_8(port, offset, data);
}

/**
 * rio_local_write_config_8 - Write 8 bits to local configuration space
 * @port: Master port
 * @offset: Offset into local configuration space
 * @data: Data to be written
 *
 * Writes 8 bits of data to the specified offset within the local
 * device's configuration space.
 */
static inline int rio_local_write_config_8(struct rio_mport *port, u32 offset,
					   u8 data)
{
	return __rio_local_write_config_8(port, offset, data);
}

/**
 * rio_read_config_32 - Read 32 bits from configuration space
 * @rdev: RIO device
 * @offset: Offset into device configuration space
 * @data: Pointer to read data into
 *
 * Reads 32 bits of data from the specified offset within the
 * RIO device's configuration space.
 */
static inline int rio_read_config_32(struct rio_dev *rdev, u32 offset,
				     u32 * data)
{
	return rio_mport_read_config_32(rdev->net->hport, rdev->destid,
					rdev->hopcount, offset, data);
};

/**
 * rio_write_config_32 - Write 32 bits to configuration space
 * @rdev: RIO device
 * @offset: Offset into device configuration space
 * @data: Data to be written
 *
 * Writes 32 bits of data to the specified offset within the
 * RIO device's configuration space.
 */
static inline int rio_write_config_32(struct rio_dev *rdev, u32 offset,
				      u32 data)
{
	return rio_mport_write_config_32(rdev->net->hport, rdev->destid,
					 rdev->hopcount, offset, data);
};

/**
 * rio_read_config_16 - Read 16 bits from configuration space
 * @rdev: RIO device
 * @offset: Offset into device configuration space
 * @data: Pointer to read data into
 *
 * Reads 16 bits of data from the specified offset within the
 * RIO device's configuration space.
 */
static inline int rio_read_config_16(struct rio_dev *rdev, u32 offset,
				     u16 * data)
{
	return rio_mport_read_config_16(rdev->net->hport, rdev->destid,
					rdev->hopcount, offset, data);
};

/**
 * rio_write_config_16 - Write 16 bits to configuration space
 * @rdev: RIO device
 * @offset: Offset into device configuration space
 * @data: Data to be written
 *
 * Writes 16 bits of data to the specified offset within the
 * RIO device's configuration space.
 */
static inline int rio_write_config_16(struct rio_dev *rdev, u32 offset,
				      u16 data)
{
	return rio_mport_write_config_16(rdev->net->hport, rdev->destid,
					 rdev->hopcount, offset, data);
};

/**
 * rio_read_config_8 - Read 8 bits from configuration space
 * @rdev: RIO device
 * @offset: Offset into device configuration space
 * @data: Pointer to read data into
 *
 * Reads 8 bits of data from the specified offset within the
 * RIO device's configuration space.
 */
static inline int rio_read_config_8(struct rio_dev *rdev, u32 offset, u8 * data)
{
	return rio_mport_read_config_8(rdev->net->hport, rdev->destid,
				       rdev->hopcount, offset, data);
};

/**
 * rio_write_config_8 - Write 8 bits to configuration space
 * @rdev: RIO device
 * @offset: Offset into device configuration space
 * @data: Data to be written
 *
 * Writes 8 bits of data to the specified offset within the
 * RIO device's configuration space.
 */
static inline int rio_write_config_8(struct rio_dev *rdev, u32 offset, u8 data)
{
	return rio_mport_write_config_8(rdev->net->hport, rdev->destid,
					rdev->hopcount, offset, data);
};

extern int rio_mport_send_doorbell(struct rio_mport *mport, u16 destid,
				   u16 data);

/**
 * rio_send_doorbell - Send a doorbell message to a device
 * @rdev: RIO device
 * @data: Doorbell message data
 *
 * Send a doorbell message to a RIO device. The doorbell message
 * has a 16-bit info field provided by the @data argument.
 */
static inline int rio_send_doorbell(struct rio_dev *rdev, u16 data)
{
	return rio_mport_send_doorbell(rdev->net->hport, rdev->destid, data);
};

/**
 * rio_init_mbox_res - Initialize a RIO mailbox resource
 * @res: resource struct
 * @start: start of mailbox range
 * @end: end of mailbox range
 *
 * This function is used to initialize the fields of a resource
 * for use as a mailbox resource.  It initializes a range of
 * mailboxes using the start and end arguments.
 */
static inline void rio_init_mbox_res(struct resource *res, int start, int end)
{
	memset(res, 0, sizeof(struct resource));
	res->start = start;
	res->end = end;
	res->flags = RIO_RESOURCE_MAILBOX;
}

/**
 * rio_init_dbell_res - Initialize a RIO doorbell resource
 * @res: resource struct
 * @start: start of doorbell range
 * @end: end of doorbell range
 *
 * This function is used to initialize the fields of a resource
 * for use as a doorbell resource.  It initializes a range of
 * doorbell messages using the start and end arguments.
 */
static inline void rio_init_dbell_res(struct resource *res, u16 start, u16 end)
{
	memset(res, 0, sizeof(struct resource));
	res->start = start;
	res->end = end;
	res->flags = RIO_RESOURCE_DOORBELL;
}

/**
 * RIO_DEVICE - macro used to describe a specific RIO device
 * @dev: the 16 bit RIO device ID
 * @ven: the 16 bit RIO vendor ID
 *
 * This macro is used to create a struct rio_device_id that matches a
 * specific device.  The assembly vendor and assembly device fields
 * will be set to %RIO_ANY_ID.
 */
#define RIO_DEVICE(dev,ven) \
	.did = (dev), .vid = (ven), \
	.asm_did = RIO_ANY_ID, .asm_vid = RIO_ANY_ID

/* Mailbox management */
extern int rio_request_outb_mbox(struct rio_mport *, void *, int, int,
				 void (*)(struct rio_mport *, void *,int, int));
extern int rio_release_outb_mbox(struct rio_mport *, int);

/**
 * rio_add_outb_message - Add RIO message to an outbound mailbox queue
 * @mport: RIO master port containing the outbound queue
 * @rdev: RIO device the message is be sent to
 * @mbox: The outbound mailbox queue
 * @buffer: Pointer to the message buffer
 * @len: Length of the message buffer
 *
 * Adds a RIO message buffer to an outbound mailbox queue for
 * transmission. Returns 0 on success.
 */
static inline int rio_add_outb_message(struct rio_mport *mport,
				       struct rio_dev *rdev, int mbox,
				       void *buffer, size_t len)
{
	return mport->ops->add_outb_message(mport, rdev, mbox,
						   buffer, len);
}

extern int rio_request_inb_mbox(struct rio_mport *, void *, int, int,
				void (*)(struct rio_mport *, void *, int, int));
extern int rio_release_inb_mbox(struct rio_mport *, int);

/**
 * rio_add_inb_buffer - Add buffer to an inbound mailbox queue
 * @mport: Master port containing the inbound mailbox
 * @mbox: The inbound mailbox number
 * @buffer: Pointer to the message buffer
 *
 * Adds a buffer to an inbound mailbox queue for reception. Returns
 * 0 on success.
 */
static inline int rio_add_inb_buffer(struct rio_mport *mport, int mbox,
				     void *buffer)
{
	return mport->ops->add_inb_buffer(mport, mbox, buffer);
}

/**
 * rio_get_inb_message - Get A RIO message from an inbound mailbox queue
 * @mport: Master port containing the inbound mailbox
 * @mbox: The inbound mailbox number
 *
 * Get a RIO message from an inbound mailbox queue. Returns 0 on success.
 */
static inline void *rio_get_inb_message(struct rio_mport *mport, int mbox)
{
	return mport->ops->get_inb_message(mport, mbox);
}

/* Doorbell management */
extern int rio_request_inb_dbell(struct rio_mport *, void *, u16, u16,
				 void (*)(struct rio_mport *, void *, u16, u16, u16));
extern int rio_release_inb_dbell(struct rio_mport *, u16, u16);
extern struct resource *rio_request_outb_dbell(struct rio_dev *, u16, u16);
extern int rio_release_outb_dbell(struct rio_dev *, struct resource *);

/* Memory region management */
int rio_claim_resource(struct rio_dev *, int);
int rio_request_regions(struct rio_dev *, char *);
void rio_release_regions(struct rio_dev *);
int rio_request_region(struct rio_dev *, int, char *);
void rio_release_region(struct rio_dev *, int);

/* Port-Write management */
extern int rio_request_inb_pwrite(struct rio_dev *,
			int (*)(struct rio_dev *, union rio_pw_msg*, int));
extern int rio_release_inb_pwrite(struct rio_dev *);
extern int rio_inb_pwrite_handler(union rio_pw_msg *pw_msg);

/* LDM support */
int rio_register_driver(struct rio_driver *);
void rio_unregister_driver(struct rio_driver *);
struct rio_dev *rio_dev_get(struct rio_dev *);
void rio_dev_put(struct rio_dev *);

/**
 * rio_name - Get the unique RIO device identifier
 * @rdev: RIO device
 *
 * Get the unique RIO device identifier. Returns the device
 * identifier string.
 */
static inline const char *rio_name(struct rio_dev *rdev)
{
	return dev_name(&rdev->dev);
}

/**
 * rio_get_drvdata - Get RIO driver specific data
 * @rdev: RIO device
 *
 * Get RIO driver specific data. Returns a pointer to the
 * driver specific data.
 */
static inline void *rio_get_drvdata(struct rio_dev *rdev)
{
	return dev_get_drvdata(&rdev->dev);
}

/**
 * rio_set_drvdata - Set RIO driver specific data
 * @rdev: RIO device
 * @data: Pointer to driver specific data
 *
 * Set RIO driver specific data. device struct driver data pointer
 * is set to the @data argument.
 */
static inline void rio_set_drvdata(struct rio_dev *rdev, void *data)
{
	dev_set_drvdata(&rdev->dev, data);
}

/* Misc driver helpers */
extern u16 rio_local_get_device_id(struct rio_mport *port);
extern struct rio_dev *rio_get_device(u16 vid, u16 did, struct rio_dev *from);
extern struct rio_dev *rio_get_asm(u16 vid, u16 did, u16 asm_vid, u16 asm_did,
				   struct rio_dev *from);

#endif				/* LINUX_RIO_DRV_H */
