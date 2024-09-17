/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2018-2021 Intel Corporation */

#ifndef __LINUX_PECI_H
#define __LINUX_PECI_H

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

/*
 * Currently we don't support any PECI command over 32 bytes.
 */
#define PECI_REQUEST_MAX_BUF_SIZE 32

struct peci_controller;
struct peci_request;

/**
 * struct peci_controller_ops - PECI controller specific methods
 * @xfer: PECI transfer function
 *
 * PECI controllers may have different hardware interfaces - the drivers
 * implementing PECI controllers can use this structure to abstract away those
 * differences by exposing a common interface for PECI core.
 */
struct peci_controller_ops {
	int (*xfer)(struct peci_controller *controller, u8 addr, struct peci_request *req);
};

/**
 * struct peci_controller - PECI controller
 * @dev: device object to register PECI controller to the device model
 * @ops: pointer to device specific controller operations
 * @bus_lock: lock used to protect multiple callers
 * @id: PECI controller ID
 *
 * PECI controllers usually connect to their drivers using non-PECI bus,
 * such as the platform bus.
 * Each PECI controller can communicate with one or more PECI devices.
 */
struct peci_controller {
	struct device dev;
	struct peci_controller_ops *ops;
	struct mutex bus_lock; /* held for the duration of xfer */
	u8 id;
};

struct peci_controller *devm_peci_controller_add(struct device *parent,
						 struct peci_controller_ops *ops);

static inline struct peci_controller *to_peci_controller(void *d)
{
	return container_of(d, struct peci_controller, dev);
}

/**
 * struct peci_device - PECI device
 * @dev: device object to register PECI device to the device model
 * @controller: manages the bus segment hosting this PECI device
 * @info: PECI device characteristics
 * @info.family: device family
 * @info.model: device model
 * @info.peci_revision: PECI revision supported by the PECI device
 * @info.socket_id: the socket ID represented by the PECI device
 * @addr: address used on the PECI bus connected to the parent controller
 * @deleted: indicates that PECI device was already deleted
 *
 * A peci_device identifies a single device (i.e. CPU) connected to a PECI bus.
 * The behaviour exposed to the rest of the system is defined by the PECI driver
 * managing the device.
 */
struct peci_device {
	struct device dev;
	struct {
		u16 family;
		u8 model;
		u8 peci_revision;
		u8 socket_id;
	} info;
	u8 addr;
	bool deleted;
};

static inline struct peci_device *to_peci_device(struct device *d)
{
	return container_of(d, struct peci_device, dev);
}

/**
 * struct peci_request - PECI request
 * @device: PECI device to which the request is sent
 * @tx: TX buffer specific data
 * @tx.buf: TX buffer
 * @tx.len: transfer data length in bytes
 * @rx: RX buffer specific data
 * @rx.buf: RX buffer
 * @rx.len: received data length in bytes
 *
 * A peci_request represents a request issued by PECI originator (TX) and
 * a response received from PECI responder (RX).
 */
struct peci_request {
	struct peci_device *device;
	struct {
		u8 buf[PECI_REQUEST_MAX_BUF_SIZE];
		u8 len;
	} rx, tx;
};

#endif /* __LINUX_PECI_H */
