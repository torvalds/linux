/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ChromeOS Embedded Controller protocol interface.
 *
 * Copyright (C) 2012 Google, Inc
 */

#ifndef __LINUX_CROS_EC_PROTO_H
#define __LINUX_CROS_EC_PROTO_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/notifier.h>

#include <linux/platform_data/cros_ec_commands.h>

#define CROS_EC_DEV_NAME	"cros_ec"
#define CROS_EC_DEV_FP_NAME	"cros_fp"
#define CROS_EC_DEV_ISH_NAME	"cros_ish"
#define CROS_EC_DEV_PD_NAME	"cros_pd"
#define CROS_EC_DEV_SCP_NAME	"cros_scp"
#define CROS_EC_DEV_TP_NAME	"cros_tp"

/*
 * The EC is unresponsive for a time after a reboot command.  Add a
 * simple delay to make sure that the bus stays locked.
 */
#define EC_REBOOT_DELAY_MS		50

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
	EC_PROTO2_MSG_BYTES	= EC_PROTO2_MAX_PARAM_SIZE +
				  EC_MSG_TX_PROTO_BYTES,

	EC_MAX_MSG_BYTES	= 64 * 1024,
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
	uint8_t data[];
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
 * @mkbp_event_supported: 0 if MKBP not supported. Otherwise its value is
 *                        the maximum supported version of the MKBP host event
 *                        command + 1.
 * @host_sleep_v1: True if this EC supports the sleep v1 command.
 * @event_notifier: Interrupt event notifier for transport devices.
 * @event_data: Raw payload transferred with the MKBP event.
 * @event_size: Size in bytes of the event data.
 * @host_event_wake_mask: Mask of host events that cause wake from suspend.
 * @last_event_time: exact time from the hard irq when we got notified of
 *     a new event.
 * @notifier_ready: The notifier_block to let the kernel re-query EC
 *		    communication protocol when the EC sends
 *		    EC_HOST_EVENT_INTERFACE_READY.
 * @ec: The platform_device used by the mfd driver to interface with the
 *      main EC.
 * @pd: The platform_device used by the mfd driver to interface with the
 *      PD behind an EC.
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
	u8 mkbp_event_supported;
	bool host_sleep_v1;
	struct blocking_notifier_head event_notifier;

	struct ec_response_get_next_event_v1 event_data;
	int event_size;
	u32 host_event_wake_mask;
	u32 last_resume_result;
	ktime_t last_event_time;
	struct notifier_block notifier_ready;

	/* The platform devices used by the mfd driver */
	struct platform_device *ec;
	struct platform_device *pd;
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

/**
 * struct cros_ec_dev - ChromeOS EC device entry point.
 * @class_dev: Device structure used in sysfs.
 * @ec_dev: cros_ec_device structure to talk to the physical device.
 * @dev: Pointer to the platform device.
 * @debug_info: cros_ec_debugfs structure for debugging information.
 * @has_kb_wake_angle: True if at least 2 accelerometer are connected to the EC.
 * @cmd_offset: Offset to apply for each command.
 * @features: Features supported by the EC.
 */
struct cros_ec_dev {
	struct device class_dev;
	struct cros_ec_device *ec_dev;
	struct device *dev;
	struct cros_ec_debugfs *debug_info;
	bool has_kb_wake_angle;
	u16 cmd_offset;
	u32 features[2];
};

#define to_cros_ec_dev(dev)  container_of(dev, struct cros_ec_dev, class_dev)

int cros_ec_prepare_tx(struct cros_ec_device *ec_dev,
		       struct cros_ec_command *msg);

int cros_ec_check_result(struct cros_ec_device *ec_dev,
			 struct cros_ec_command *msg);

int cros_ec_cmd_xfer_status(struct cros_ec_device *ec_dev,
			    struct cros_ec_command *msg);

int cros_ec_query_all(struct cros_ec_device *ec_dev);

int cros_ec_get_next_event(struct cros_ec_device *ec_dev,
			   bool *wake_event,
			   bool *has_more_events);

u32 cros_ec_get_host_event(struct cros_ec_device *ec_dev);

bool cros_ec_check_features(struct cros_ec_dev *ec, int feature);

int cros_ec_get_sensor_count(struct cros_ec_dev *ec);

int cros_ec_command(struct cros_ec_device *ec_dev, unsigned int version, int command, void *outdata,
		    int outsize, void *indata, int insize);

/**
 * cros_ec_get_time_ns() - Return time in ns.
 *
 * This is the function used to record the time for last_event_time in struct
 * cros_ec_device during the hard irq.
 *
 * Return: ktime_t format since boot.
 */
static inline ktime_t cros_ec_get_time_ns(void)
{
	return ktime_get_boottime_ns();
}

#endif /* __LINUX_CROS_EC_PROTO_H */
