/*
 * ChromeOS EC multi-function device
 *
 * Copyright (C) 2012 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_MFD_CROS_EC_H
#define __LINUX_MFD_CROS_EC_H

#include <linux/notifier.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/mutex.h>

/*
 * Command interface between EC and AP, for LPC, I2C and SPI interfaces.
 */
enum {
	EC_MSG_TX_HEADER_BYTES	= 3,
	EC_MSG_TX_TRAILER_BYTES	= 1,
	EC_MSG_TX_PROTO_BYTES	= EC_MSG_TX_HEADER_BYTES +
					EC_MSG_TX_TRAILER_BYTES,
	EC_MSG_RX_PROTO_BYTES	= 3,

	/* Max length of messages */
	EC_MSG_BYTES		= EC_PROTO2_MAX_PARAM_SIZE +
					EC_MSG_TX_PROTO_BYTES,
};

/*
 * @version: Command version number (often 0)
 * @command: Command to send (EC_CMD_...)
 * @outdata: Outgoing data to EC
 * @outsize: Outgoing length in bytes
 * @indata: Where to put the incoming data from EC
 * @insize: Incoming length in bytes (filled in by EC)
 * @result: EC's response to the command (separate from communication failure)
 */
struct cros_ec_command {
	uint32_t version;
	uint32_t command;
	uint8_t *outdata;
	uint32_t outsize;
	uint8_t *indata;
	uint32_t insize;
	uint32_t result;
};

/**
 * struct cros_ec_device - Information about a ChromeOS EC device
 *
 * @ec_name: name of EC device (e.g. 'chromeos-ec')
 * @phys_name: name of physical comms layer (e.g. 'i2c-4')
 * @dev: Device pointer
 * @was_wake_device: true if this device was set to wake the system from
 * sleep at the last suspend
 * @event_notifier: interrupt event notifier for transport devices
 * @command_send: send a command
 * @command_recv: receive a response
 * @command_sendrecv: send a command and receive a response
 *
 * @name: Name of this EC interface
 * @priv: Private data
 * @irq: Interrupt to use
 * @din: input buffer (for data from EC)
 * @dout: output buffer (for data to EC)
 * \note
 * These two buffers will always be dword-aligned and include enough
 * space for up to 7 word-alignment bytes also, so we can ensure that
 * the body of the message is always dword-aligned (64-bit).
 * We use this alignment to keep ARM and x86 happy. Probably word
 * alignment would be OK, there might be a small performance advantage
 * to using dword.
 * @din_size: size of din buffer to allocate (zero to use static din)
 * @dout_size: size of dout buffer to allocate (zero to use static dout)
 * @parent: pointer to parent device (e.g. i2c or spi device)
 * @wake_enabled: true if this device can wake the system from sleep
 * @lock: one transaction at a time
 * @cmd_xfer: low-level channel to the EC
 */
struct cros_ec_device {

	/* These are used by other drivers that want to talk to the EC */
	const char *ec_name;
	const char *phys_name;
	struct device *dev;
	bool was_wake_device;
	struct class *cros_class;
	struct blocking_notifier_head event_notifier;
	int (*command_send)(struct cros_ec_device *ec,
			    uint16_t cmd, void *out_buf, int out_len);
	int (*command_recv)(struct cros_ec_device *ec,
			    uint16_t cmd, void *in_buf, int in_len);
	int (*command_sendrecv)(struct cros_ec_device *ec,
				uint16_t cmd, void *out_buf, int out_len,
				void *in_buf, int in_len);

	/* These are used to implement the platform-specific interface */
	const char *name;
	void *priv;
	int irq;
	uint8_t *din;
	uint8_t *dout;
	int din_size;
	int dout_size;
	struct device *parent;
	bool wake_enabled;
	struct mutex lock;
	int (*cmd_xfer)(struct cros_ec_device *ec,
			struct cros_ec_command *msg);
};

/**
 * cros_ec_suspend - Handle a suspend operation for the ChromeOS EC device
 *
 * This can be called by drivers to handle a suspend event.
 *
 * ec_dev: Device to suspend
 * @return 0 if ok, -ve on error
 */
int cros_ec_suspend(struct cros_ec_device *ec_dev);

/**
 * cros_ec_resume - Handle a resume operation for the ChromeOS EC device
 *
 * This can be called by drivers to handle a resume event.
 *
 * @ec_dev: Device to resume
 * @return 0 if ok, -ve on error
 */
int cros_ec_resume(struct cros_ec_device *ec_dev);

/**
 * cros_ec_prepare_tx - Prepare an outgoing message in the output buffer
 *
 * This is intended to be used by all ChromeOS EC drivers, but at present
 * only SPI uses it. Once LPC uses the same protocol it can start using it.
 * I2C could use it now, with a refactor of the existing code.
 *
 * @ec_dev: Device to register
 * @msg: Message to write
 */
int cros_ec_prepare_tx(struct cros_ec_device *ec_dev,
		       struct cros_ec_command *msg);

/**
 * cros_ec_remove - Remove a ChromeOS EC
 *
 * Call this to deregister a ChromeOS EC, then clean up any private data.
 *
 * @ec_dev: Device to register
 * @return 0 if ok, -ve on error
 */
int cros_ec_remove(struct cros_ec_device *ec_dev);

/**
 * cros_ec_register - Register a new ChromeOS EC, using the provided info
 *
 * Before calling this, allocate a pointer to a new device and then fill
 * in all the fields up to the --private-- marker.
 *
 * @ec_dev: Device to register
 * @return 0 if ok, -ve on error
 */
int cros_ec_register(struct cros_ec_device *ec_dev);

#endif /* __LINUX_MFD_CROS_EC_H */
