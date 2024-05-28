/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Intel Corporation. All rights reserved.
 */
#ifndef _LINUX_USB_LJCA_H_
#define _LINUX_USB_LJCA_H_

#include <linux/auxiliary_bus.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define LJCA_MAX_GPIO_NUM 64

#define auxiliary_dev_to_ljca_client(auxiliary_dev)			\
		container_of(auxiliary_dev, struct ljca_client, auxdev)

struct ljca_adapter;

/**
 * typedef ljca_event_cb_t - event callback function signature
 *
 * @context: the execution context of who registered this callback
 * @cmd: the command from device for this event
 * @evt_data: the event data payload
 * @len: the event data payload length
 *
 * The callback function is called in interrupt context and the data payload is
 * only valid during the call. If the user needs later access of the data, it
 * must copy it.
 */
typedef void (*ljca_event_cb_t)(void *context, u8 cmd, const void *evt_data, int len);

/**
 * struct ljca_client - represent a ljca client device
 *
 * @type: ljca client type
 * @id: ljca client id within same client type
 * @link: ljca client on the same ljca adapter
 * @auxdev: auxiliary device object
 * @adapter: ljca adapter the ljca client sit on
 * @context: the execution context of the event callback
 * @event_cb: ljca client driver register this callback to get
 *	firmware asynchronous rx buffer pending notifications
 * @event_cb_lock: spinlock to protect event callback
 */
struct ljca_client {
	u8 type;
	u8 id;
	struct list_head link;
	struct auxiliary_device auxdev;
	struct ljca_adapter *adapter;

	void *context;
	ljca_event_cb_t event_cb;
	/* lock to protect event_cb */
	spinlock_t event_cb_lock;
};

/**
 * struct ljca_gpio_info - ljca gpio client device info
 *
 * @num: ljca gpio client device pin number
 * @valid_pin_map: ljca gpio client device valid pin mapping
 */
struct ljca_gpio_info {
	unsigned int num;
	DECLARE_BITMAP(valid_pin_map, LJCA_MAX_GPIO_NUM);
};

/**
 * struct ljca_i2c_info - ljca i2c client device info
 *
 * @id: ljca i2c client device identification number
 * @capacity: ljca i2c client device capacity
 * @intr_pin: ljca i2c client device interrupt pin number if exists
 */
struct ljca_i2c_info {
	u8 id;
	u8 capacity;
	u8 intr_pin;
};

/**
 * struct ljca_spi_info - ljca spi client device info
 *
 * @id: ljca spi client device identification number
 * @capacity: ljca spi client device capacity
 */
struct ljca_spi_info {
	u8 id;
	u8 capacity;
};

/**
 * ljca_register_event_cb - register a callback function to receive events
 *
 * @client: ljca client device
 * @event_cb: callback function
 * @context: execution context of event callback
 *
 * Return: 0 in case of success, negative value in case of error
 */
int ljca_register_event_cb(struct ljca_client *client, ljca_event_cb_t event_cb, void *context);

/**
 * ljca_unregister_event_cb - unregister the callback function for an event
 *
 * @client: ljca client device
 */
void ljca_unregister_event_cb(struct ljca_client *client);

/**
 * ljca_transfer - issue a LJCA command and wait for a response
 *
 * @client: ljca client device
 * @cmd: the command to be sent to the device
 * @obuf: the buffer to be sent to the device; it can be NULL if the user
 *	doesn't need to transmit data with this command
 * @obuf_len: the size of the buffer to be sent to the device; it should
 *	be 0 when obuf is NULL
 * @ibuf: any data associated with the response will be copied here; it can be
 *	NULL if the user doesn't need the response data
 * @ibuf_len: must be initialized to the input buffer size
 *
 * Return: the actual length of response data for success, negative value for errors
 */
int ljca_transfer(struct ljca_client *client, u8 cmd, const u8 *obuf,
		  u8 obuf_len, u8 *ibuf, u8 ibuf_len);

/**
 * ljca_transfer_noack - issue a LJCA command without a response
 *
 * @client: ljca client device
 * @cmd: the command to be sent to the device
 * @obuf: the buffer to be sent to the device; it can be NULL if the user
 *	doesn't need to transmit data with this command
 * @obuf_len: the size of the buffer to be sent to the device
 *
 * Return: 0 for success, negative value for errors
 */
int ljca_transfer_noack(struct ljca_client *client, u8 cmd, const u8 *obuf,
			u8 obuf_len);

#endif
