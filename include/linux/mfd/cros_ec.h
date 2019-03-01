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

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/mutex.h>

#define CROS_EC_DEV_NAME "cros_ec"
#define CROS_EC_DEV_PD_NAME "cros_pd"
#define CROS_EC_DEV_ISH_NAME "cros_ish"

/*
 * The EC is unresponsive for a time after a reboot command.  Add a
 * simple delay to make sure that the bus stays locked.
 */
#define EC_REBOOT_DELAY_MS             50

/*
 * Max bus-specific overhead incurred by request/responses.
 * I2C requires 1 additional byte for requests.
 * I2C requires 2 additional bytes for responses.
 * SPI requires up to 32 additional bytes for responses.
 */
#define EC_PROTO_VERSION_UNKNOWN	0
#define EC_MAX_REQUEST_OVERHEAD		1
#define EC_MAX_RESPONSE_OVERHEAD	32

/*
 * Command interface between EC and AP, for LPC, I2C and SPI interfaces.
 */
enum {
	EC_MSG_TX_HEADER_BYTES	= 3,
	EC_MSG_TX_TRAILER_BYTES	= 1,
	EC_MSG_TX_PROTO_BYTES	= EC_MSG_TX_HEADER_BYTES +
					EC_MSG_TX_TRAILER_BYTES,
	EC_MSG_RX_PROTO_BYTES	= 3,

	/* Max length of messages for proto 2*/
	EC_PROTO2_MSG_BYTES		= EC_PROTO2_MAX_PARAM_SIZE +
					EC_MSG_TX_PROTO_BYTES,

	EC_MAX_MSG_BYTES		= 64 * 1024,
};

/**
 * struct cros_ec_command - Information about a ChromeOS EC command.
 * @version: Command version number (often 0).
 * @command: Command to send (EC_CMD_...).
 * @outsize: Outgoing length in bytes.
 * @insize: Max number of bytes to accept from the EC.
 * @result: EC's response to the command (separate from communication failure).
 * @data: Where to put the incoming data from EC and outgoing data to EC.
 */
struct cros_ec_command {
	uint32_t version;
	uint32_t command;
	uint32_t outsize;
	uint32_t insize;
	uint32_t result;
	uint8_t data[0];
};

/**
 * struct cros_ec_device - Information about a ChromeOS EC device.
 * @phys_name: Name of physical comms layer (e.g. 'i2c-4').
 * @dev: Device pointer for physical comms device
 * @was_wake_device: True if this device was set to wake the system from
 *                   sleep at the last suspend.
 * @cros_class: The class structure for this device.
 * @cmd_readmem: Direct read of the EC memory-mapped region, if supported.
 *     @offset: Is within EC_LPC_ADDR_MEMMAP region.
 *     @bytes: Number of bytes to read. zero means "read a string" (including
 *             the trailing '\0'). At most only EC_MEMMAP_SIZE bytes can be
 *             read. Caller must ensure that the buffer is large enough for the
 *             result when reading a string.
 * @max_request: Max size of message requested.
 * @max_response: Max size of message response.
 * @max_passthru: Max sice of passthru message.
 * @proto_version: The protocol version used for this device.
 * @priv: Private data.
 * @irq: Interrupt to use.
 * @id: Device id.
 * @din: Input buffer (for data from EC). This buffer will always be
 *       dword-aligned and include enough space for up to 7 word-alignment
 *       bytes also, so we can ensure that the body of the message is always
 *       dword-aligned (64-bit). We use this alignment to keep ARM and x86
 *       happy. Probably word alignment would be OK, there might be a small
 *       performance advantage to using dword.
 * @dout: Output buffer (for data to EC). This buffer will always be
 *        dword-aligned and include enough space for up to 7 word-alignment
 *        bytes also, so we can ensure that the body of the message is always
 *        dword-aligned (64-bit). We use this alignment to keep ARM and x86
 *        happy. Probably word alignment would be OK, there might be a small
 *        performance advantage to using dword.
 * @din_size: Size of din buffer to allocate (zero to use static din).
 * @dout_size: Size of dout buffer to allocate (zero to use static dout).
 * @wake_enabled: True if this device can wake the system from sleep.
 * @suspended: True if this device had been suspended.
 * @cmd_xfer: Send command to EC and get response.
 *            Returns the number of bytes received if the communication
 *            succeeded, but that doesn't mean the EC was happy with the
 *            command. The caller should check msg.result for the EC's result
 *            code.
 * @pkt_xfer: Send packet to EC and get response.
 * @lock: One transaction at a time.
 * @mkbp_event_supported: True if this EC supports the MKBP event protocol.
 * @event_notifier: Interrupt event notifier for transport devices.
 * @event_data: Raw payload transferred with the MKBP event.
 * @event_size: Size in bytes of the event data.
 * @host_event_wake_mask: Mask of host events that cause wake from suspend.
 */
struct cros_ec_device {
	/* These are used by other drivers that want to talk to the EC */
	const char *phys_name;
	struct device *dev;
	bool was_wake_device;
	struct class *cros_class;
	int (*cmd_readmem)(struct cros_ec_device *ec, unsigned int offset,
			   unsigned int bytes, void *dest);

	/* These are used to implement the platform-specific interface */
	u16 max_request;
	u16 max_response;
	u16 max_passthru;
	u16 proto_version;
	void *priv;
	int irq;
	u8 *din;
	u8 *dout;
	int din_size;
	int dout_size;
	bool wake_enabled;
	bool suspended;
	int (*cmd_xfer)(struct cros_ec_device *ec,
			struct cros_ec_command *msg);
	int (*pkt_xfer)(struct cros_ec_device *ec,
			struct cros_ec_command *msg);
	struct mutex lock;
	bool mkbp_event_supported;
	struct blocking_notifier_head event_notifier;

	struct ec_response_get_next_event_v1 event_data;
	int event_size;
	u32 host_event_wake_mask;
};

/**
 * struct cros_ec_sensor_platform - ChromeOS EC sensor platform information.
 * @sensor_num: Id of the sensor, as reported by the EC.
 */
struct cros_ec_sensor_platform {
	u8 sensor_num;
};

/**
 * struct cros_ec_platform - ChromeOS EC platform information.
 * @ec_name: Name of EC device (e.g. 'cros-ec', 'cros-pd', ...)
 *           used in /dev/ and sysfs.
 * @cmd_offset: Offset to apply for each command. Set when
 *              registering a device behind another one.
 */
struct cros_ec_platform {
	const char *ec_name;
	u16 cmd_offset;
};

struct cros_ec_debugfs;

/**
 * struct cros_ec_dev - ChromeOS EC device entry point.
 * @class_dev: Device structure used in sysfs.
 * @cdev: Character device structure in /dev.
 * @ec_dev: cros_ec_device structure to talk to the physical device.
 * @dev: Pointer to the platform device.
 * @debug_info: cros_ec_debugfs structure for debugging information.
 * @has_kb_wake_angle: True if at least 2 accelerometer are connected to the EC.
 * @cmd_offset: Offset to apply for each command.
 * @features: Features supported by the EC.
 */
struct cros_ec_dev {
	struct device class_dev;
	struct cdev cdev;
	struct cros_ec_device *ec_dev;
	struct device *dev;
	struct cros_ec_debugfs *debug_info;
	bool has_kb_wake_angle;
	u16 cmd_offset;
	u32 features[2];
};

#define to_cros_ec_dev(dev)  container_of(dev, struct cros_ec_dev, class_dev)

/**
 * cros_ec_suspend() - Handle a suspend operation for the ChromeOS EC device.
 * @ec_dev: Device to suspend.
 *
 * This can be called by drivers to handle a suspend event.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_suspend(struct cros_ec_device *ec_dev);

/**
 * cros_ec_resume() - Handle a resume operation for the ChromeOS EC device.
 * @ec_dev: Device to resume.
 *
 * This can be called by drivers to handle a resume event.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_resume(struct cros_ec_device *ec_dev);

/**
 * cros_ec_prepare_tx() - Prepare an outgoing message in the output buffer.
 * @ec_dev: Device to register.
 * @msg: Message to write.
 *
 * This is intended to be used by all ChromeOS EC drivers, but at present
 * only SPI uses it. Once LPC uses the same protocol it can start using it.
 * I2C could use it now, with a refactor of the existing code.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_prepare_tx(struct cros_ec_device *ec_dev,
		       struct cros_ec_command *msg);

/**
 * cros_ec_check_result() - Check ec_msg->result.
 * @ec_dev: EC device.
 * @msg: Message to check.
 *
 * This is used by ChromeOS EC drivers to check the ec_msg->result for
 * errors and to warn about them.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_check_result(struct cros_ec_device *ec_dev,
			 struct cros_ec_command *msg);

/**
 * cros_ec_cmd_xfer() - Send a command to the ChromeOS EC.
 * @ec_dev: EC device.
 * @msg: Message to write.
 *
 * Call this to send a command to the ChromeOS EC.  This should be used
 * instead of calling the EC's cmd_xfer() callback directly.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_cmd_xfer(struct cros_ec_device *ec_dev,
		     struct cros_ec_command *msg);

/**
 * cros_ec_cmd_xfer_status() - Send a command to the ChromeOS EC.
 * @ec_dev: EC device.
 * @msg: Message to write.
 *
 * This function is identical to cros_ec_cmd_xfer, except it returns success
 * status only if both the command was transmitted successfully and the EC
 * replied with success status. It's not necessary to check msg->result when
 * using this function.
 *
 * Return: The number of bytes transferred on success or negative error code.
 */
int cros_ec_cmd_xfer_status(struct cros_ec_device *ec_dev,
			    struct cros_ec_command *msg);

/**
 * cros_ec_register() - Register a new ChromeOS EC, using the provided info.
 * @ec_dev: Device to register.
 *
 * Before calling this, allocate a pointer to a new device and then fill
 * in all the fields up to the --private-- marker.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_register(struct cros_ec_device *ec_dev);

/**
 * cros_ec_query_all() -  Query the protocol version supported by the
 *         ChromeOS EC.
 * @ec_dev: Device to register.
 *
 * Return: 0 on success or negative error code.
 */
int cros_ec_query_all(struct cros_ec_device *ec_dev);

/**
 * cros_ec_get_next_event() - Fetch next event from the ChromeOS EC.
 * @ec_dev: Device to fetch event from.
 * @wake_event: Pointer to a bool set to true upon return if the event might be
 *              treated as a wake event. Ignored if null.
 *
 * Return: negative error code on errors; 0 for no data; or else number of
 * bytes received (i.e., an event was retrieved successfully). Event types are
 * written out to @ec_dev->event_data.event_type on success.
 */
int cros_ec_get_next_event(struct cros_ec_device *ec_dev, bool *wake_event);

/**
 * cros_ec_get_host_event() - Return a mask of event set by the ChromeOS EC.
 * @ec_dev: Device to fetch event from.
 *
 * When MKBP is supported, when the EC raises an interrupt, we collect the
 * events raised and call the functions in the ec notifier. This function
 * is a helper to know which events are raised.
 *
 * Return: 0 on error or non-zero bitmask of one or more EC_HOST_EVENT_*.
 */
u32 cros_ec_get_host_event(struct cros_ec_device *ec_dev);

#endif /* __LINUX_MFD_CROS_EC_H */
