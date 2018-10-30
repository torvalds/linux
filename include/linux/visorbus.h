// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 */

/*
 *  This header file is to be included by other kernel mode components that
 *  implement a particular kind of visor_device.  Each of these other kernel
 *  mode components is called a visor device driver.  Refer to visortemplate
 *  for a minimal sample visor device driver.
 *
 *  There should be nothing in this file that is private to the visorbus
 *  bus implementation itself.
 */

#ifndef __VISORBUS_H__
#define __VISORBUS_H__

#include <linux/device.h>

#define VISOR_CHANNEL_SIGNATURE ('L' << 24 | 'N' << 16 | 'C' << 8 | 'E')

/*
 * enum channel_serverstate
 * @CHANNELSRV_UNINITIALIZED: Channel is in an undefined state.
 * @CHANNELSRV_READY:	      Channel has been initialized by server.
 */
enum channel_serverstate {
	CHANNELSRV_UNINITIALIZED = 0,
	CHANNELSRV_READY = 1
};

/*
 * enum channel_clientstate
 * @CHANNELCLI_DETACHED:
 * @CHANNELCLI_DISABLED:  Client can see channel but is NOT allowed to use it
 *			  unless given TBD* explicit request
 *			  (should actually be < DETACHED).
 * @CHANNELCLI_ATTACHING: Legacy EFI client request for EFI server to attach.
 * @CHANNELCLI_ATTACHED:  Idle, but client may want to use channel any time.
 * @CHANNELCLI_BUSY:	  Client either wants to use or is using channel.
 * @CHANNELCLI_OWNED:	  "No worries" state - client can access channel
 *			  anytime.
 */
enum channel_clientstate {
	CHANNELCLI_DETACHED = 0,
	CHANNELCLI_DISABLED = 1,
	CHANNELCLI_ATTACHING = 2,
	CHANNELCLI_ATTACHED = 3,
	CHANNELCLI_BUSY = 4,
	CHANNELCLI_OWNED = 5
};

/*
 * Values for VISOR_CHANNEL_PROTOCOL.Features: This define exists so that
 * a guest can look at the FeatureFlags in the io channel, and configure the
 * driver to use interrupts or not based on this setting. All feature bits for
 * all channels should be defined here. The io channel feature bits are defined
 * below.
 */
#define VISOR_DRIVER_ENABLES_INTS (0x1ULL << 1)
#define VISOR_CHANNEL_IS_POLLING (0x1ULL << 3)
#define VISOR_IOVM_OK_DRIVER_DISABLING_INTS (0x1ULL << 4)
#define VISOR_DRIVER_DISABLES_INTS (0x1ULL << 5)
#define VISOR_DRIVER_ENHANCED_RCVBUF_CHECKING (0x1ULL << 6)

/*
 * struct channel_header - Common Channel Header
 * @signature:	       Signature.
 * @legacy_state:      DEPRECATED - being replaced by.
 * @header_size:       sizeof(struct channel_header).
 * @size:	       Total size of this channel in bytes.
 * @features:	       Flags to modify behavior.
 * @chtype:	       Channel type: data, bus, control, etc..
 * @partition_handle:  ID of guest partition.
 * @handle:	       Device number of this channel in client.
 * @ch_space_offset:   Offset in bytes to channel specific area.
 * @version_id:	       Struct channel_header Version ID.
 * @partition_index:   Index of guest partition.
 * @zone_uuid:	       Guid of Channel's zone.
 * @cli_str_offset:    Offset from channel header to null-terminated
 *		       ClientString (0 if ClientString not present).
 * @cli_state_boot:    CHANNEL_CLIENTSTATE of pre-boot EFI client of this
 *		       channel.
 * @cmd_state_cli:     CHANNEL_COMMANDSTATE (overloaded in Windows drivers, see
 *		       ServerStateUp, ServerStateDown, etc).
 * @cli_state_os:      CHANNEL_CLIENTSTATE of Guest OS client of this channel.
 * @ch_characteristic: CHANNEL_CHARACTERISTIC_<xxx>.
 * @cmd_state_srv:     CHANNEL_COMMANDSTATE (overloaded in Windows drivers, see
 *		       ServerStateUp, ServerStateDown, etc).
 * @srv_state:	       CHANNEL_SERVERSTATE.
 * @cli_error_boot:    Bits to indicate err states for boot clients, so err
 *		       messages can be throttled.
 * @cli_error_os:      Bits to indicate err states for OS clients, so err
 *		       messages can be throttled.
 * @filler:	       Pad out to 128 byte cacheline.
 * @recover_channel:   Please add all new single-byte values below here.
 */
struct channel_header {
	u64 signature;
	u32 legacy_state;
	/* SrvState, CliStateBoot, and CliStateOS below */
	u32 header_size;
	u64 size;
	u64 features;
	guid_t chtype;
	u64 partition_handle;
	u64 handle;
	u64 ch_space_offset;
	u32 version_id;
	u32 partition_index;
	guid_t zone_guid;
	u32 cli_str_offset;
	u32 cli_state_boot;
	u32 cmd_state_cli;
	u32 cli_state_os;
	u32 ch_characteristic;
	u32 cmd_state_srv;
	u32 srv_state;
	u8 cli_error_boot;
	u8 cli_error_os;
	u8 filler[1];
	u8 recover_channel;
} __packed;

#define VISOR_CHANNEL_ENABLE_INTS (0x1ULL << 0)

/*
 * struct signal_queue_header - Subheader for the Signal Type variation of the
 *                              Common Channel.
 * @version:	      SIGNAL_QUEUE_HEADER Version ID.
 * @chtype:	      Queue type: storage, network.
 * @size:	      Total size of this queue in bytes.
 * @sig_base_offset:  Offset to signal queue area.
 * @features:	      Flags to modify behavior.
 * @num_sent:	      Total # of signals placed in this queue.
 * @num_overflows:    Total # of inserts failed due to full queue.
 * @signal_size:      Total size of a signal for this queue.
 * @max_slots:        Max # of slots in queue, 1 slot is always empty.
 * @max_signals:      Max # of signals in queue (MaxSignalSlots-1).
 * @head:	      Queue head signal #.
 * @num_received:     Total # of signals removed from this queue.
 * @tail:	      Queue tail signal.
 * @reserved1:	      Reserved field.
 * @reserved2:	      Reserved field.
 * @client_queue:
 * @num_irq_received: Total # of Interrupts received. This is incremented by the
 *		      ISR in the guest windows driver.
 * @num_empty:	      Number of times that visor_signal_remove is called and
 *		      returned Empty Status.
 * @errorflags:	      Error bits set during SignalReinit to denote trouble with
 *		      client's fields.
 * @filler:	      Pad out to 64 byte cacheline.
 */
struct signal_queue_header {
	/* 1st cache line */
	u32 version;
	u32 chtype;
	u64 size;
	u64 sig_base_offset;
	u64 features;
	u64 num_sent;
	u64 num_overflows;
	u32 signal_size;
	u32 max_slots;
	u32 max_signals;
	u32 head;
	/* 2nd cache line */
	u64 num_received;
	u32 tail;
	u32 reserved1;
	u64 reserved2;
	u64 client_queue;
	u64 num_irq_received;
	u64 num_empty;
	u32 errorflags;
	u8 filler[12];
} __packed;

/* VISORCHANNEL Guids */
/* {414815ed-c58c-11da-95a9-00e08161165f} */
#define VISOR_VHBA_CHANNEL_GUID \
	GUID_INIT(0x414815ed, 0xc58c, 0x11da, \
		  0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
#define VISOR_VHBA_CHANNEL_GUID_STR \
	"414815ed-c58c-11da-95a9-00e08161165f"
struct visorchipset_state {
	u32 created:1;
	u32 attached:1;
	u32 configured:1;
	u32 running:1;
	/* Remaining bits in this 32-bit word are reserved. */
};

/**
 * struct visor_device - A device type for things "plugged" into the visorbus
 *                       bus
 * @visorchannel:		Points to the channel that the device is
 *				associated with.
 * @channel_type_guid:		Identifies the channel type to the bus driver.
 * @device:			Device struct meant for use by the bus driver
 *				only.
 * @list_all:			Used by the bus driver to enumerate devices.
 * @timer:		        Timer fired periodically to do interrupt-type
 *				activity.
 * @being_removed:		Indicates that the device is being removed from
 *				the bus. Private bus driver use only.
 * @visordriver_callback_lock:	Used by the bus driver to lock when adding and
 *				removing devices.
 * @pausing:			Indicates that a change towards a paused state.
 *				is in progress. Only modified by the bus driver.
 * @resuming:			Indicates that a change towards a running state
 *				is in progress. Only modified by the bus driver.
 * @chipset_bus_no:		Private field used by the bus driver.
 * @chipset_dev_no:		Private field used the bus driver.
 * @state:			Used to indicate the current state of the
 *				device.
 * @inst:			Unique GUID for this instance of the device.
 * @name:			Name of the device.
 * @pending_msg_hdr:		For private use by bus driver to respond to
 *				hypervisor requests.
 * @vbus_hdr_info:		A pointer to header info. Private use by bus
 *				driver.
 * @partition_guid:		Indicates client partion id. This should be the
 *				same across all visor_devices in the current
 *				guest. Private use by bus driver only.
 */
struct visor_device {
	struct visorchannel *visorchannel;
	guid_t channel_type_guid;
	/* These fields are for private use by the bus driver only. */
	struct device device;
	struct list_head list_all;
	struct timer_list timer;
	bool timer_active;
	bool being_removed;
	struct mutex visordriver_callback_lock; /* synchronize probe/remove */
	bool pausing;
	bool resuming;
	u32 chipset_bus_no;
	u32 chipset_dev_no;
	struct visorchipset_state state;
	guid_t inst;
	u8 *name;
	struct controlvm_message_header *pending_msg_hdr;
	void *vbus_hdr_info;
	guid_t partition_guid;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_bus_info;
};

#define to_visor_device(x) container_of(x, struct visor_device, device)

typedef void (*visorbus_state_complete_func) (struct visor_device *dev,
					      int status);

/*
 * This struct describes a specific visor channel, by providing its GUID, name,
 * and sizes.
 */
struct visor_channeltype_descriptor {
	const guid_t guid;
	const char *name;
	u64 min_bytes;
	u32 version;
};

/**
 * struct visor_driver - Information provided by each visor driver when it
 *                       registers with the visorbus driver
 * @name:		Name of the visor driver.
 * @owner:		The module owner.
 * @channel_types:	Types of channels handled by this driver, ending with
 *			a zero GUID. Our specialized BUS.match() method knows
 *			about this list, and uses it to determine whether this
 *			driver will in fact handle a new device that it has
 *			detected.
 * @probe:		Called when a new device comes online, by our probe()
 *			function specified by driver.probe() (triggered
 *			ultimately by some call to driver_register(),
 *			bus_add_driver(), or driver_attach()).
 * @remove:		Called when a new device is removed, by our remove()
 *			function specified by driver.remove() (triggered
 *			ultimately by some call to device_release_driver()).
 * @channel_interrupt:	Called periodically, whenever there is a possiblity
 *			that "something interesting" may have happened to the
 *			channel.
 * @pause:		Called to initiate a change of the device's state.  If
 *			the return valu`e is < 0, there was an error and the
 *			state transition will NOT occur.  If the return value
 *			is >= 0, then the state transition was INITIATED
 *			successfully, and complete_func() will be called (or
 *			was just called) with the final status when either the
 *			state transition fails or completes successfully.
 * @resume:		Behaves similar to pause.
 * @driver:		Private reference to the device driver. For use by bus
 *			driver only.
 */
struct visor_driver {
	const char *name;
	struct module *owner;
	struct visor_channeltype_descriptor *channel_types;
	int (*probe)(struct visor_device *dev);
	void (*remove)(struct visor_device *dev);
	void (*channel_interrupt)(struct visor_device *dev);
	int (*pause)(struct visor_device *dev,
		     visorbus_state_complete_func complete_func);
	int (*resume)(struct visor_device *dev,
		      visorbus_state_complete_func complete_func);

	/* These fields are for private use by the bus driver only. */
	struct device_driver driver;
};

#define to_visor_driver(x) (container_of(x, struct visor_driver, driver))

int visor_check_channel(struct channel_header *ch, struct device *dev,
			const guid_t *expected_uuid, char *chname,
			u64 expected_min_bytes,	u32 expected_version,
			u64 expected_signature);

int visorbus_register_visor_driver(struct visor_driver *drv);
void visorbus_unregister_visor_driver(struct visor_driver *drv);
int visorbus_read_channel(struct visor_device *dev,
			  unsigned long offset, void *dest,
			  unsigned long nbytes);
int visorbus_write_channel(struct visor_device *dev,
			   unsigned long offset, void *src,
			   unsigned long nbytes);
int visorbus_enable_channel_interrupts(struct visor_device *dev);
void visorbus_disable_channel_interrupts(struct visor_device *dev);

int visorchannel_signalremove(struct visorchannel *channel, u32 queue,
			      void *msg);
int visorchannel_signalinsert(struct visorchannel *channel, u32 queue,
			      void *msg);
bool visorchannel_signalempty(struct visorchannel *channel, u32 queue);
const guid_t *visorchannel_get_guid(struct visorchannel *channel);

#define BUS_ROOT_DEVICE UINT_MAX
struct visor_device *visorbus_get_device_by_id(u32 bus_no, u32 dev_no,
					       struct visor_device *from);
#endif
