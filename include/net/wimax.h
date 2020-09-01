/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux WiMAX
 * Kernel space API for accessing WiMAX devices
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * The WiMAX stack provides an API for controlling and managing the
 * system's WiMAX devices. This API affects the control plane; the
 * data plane is accessed via the network stack (netdev).
 *
 * Parts of the WiMAX stack API and notifications are exported to
 * user space via Generic Netlink. In user space, libwimax (part of
 * the wimax-tools package) provides a shim layer for accessing those
 * calls.
 *
 * The API is standarized for all WiMAX devices and different drivers
 * implement the backend support for it. However, device-specific
 * messaging pipes are provided that can be used to issue commands and
 * receive notifications in free form.
 *
 * Currently the messaging pipes are the only means of control as it
 * is not known (due to the lack of more devices in the market) what
 * will be a good abstraction layer. Expect this to change as more
 * devices show in the market. This API is designed to be growable in
 * order to address this problem.
 *
 * USAGE
 *
 * Embed a `struct wimax_dev` at the beginning of the device's
 * private structure, initialize and register it. For details, see
 * `struct wimax_dev`s documentation.
 *
 * Once this is done, wimax-tools's libwimaxll can be used to
 * communicate with the driver from user space. You user space
 * application does not have to forcibily use libwimaxll and can talk
 * the generic netlink protocol directly if desired.
 *
 * Remember this is a very low level API that will to provide all of
 * WiMAX features. Other daemons and services running in user space
 * are the expected clients of it. They offer a higher level API that
 * applications should use (an example of this is the Intel's WiMAX
 * Network Service for the i2400m).
 *
 * DESIGN
 *
 * Although not set on final stone, this very basic interface is
 * mostly completed. Remember this is meant to grow as new common
 * operations are decided upon. New operations will be added to the
 * interface, intent being on keeping backwards compatibility as much
 * as possible.
 *
 * This layer implements a set of calls to control a WiMAX device,
 * exposing a frontend to the rest of the kernel and user space (via
 * generic netlink) and a backend implementation in the driver through
 * function pointers.
 *
 * WiMAX devices have a state, and a kernel-only API allows the
 * drivers to manipulate that state. State transitions are atomic, and
 * only some of them are allowed (see `enum wimax_st`).
 *
 * Most API calls will set the state automatically; in most cases
 * drivers have to only report state changes due to external
 * conditions.
 *
 * All API operations are 'atomic', serialized through a mutex in the
 * `struct wimax_dev`.
 *
 * EXPORTING TO USER SPACE THROUGH GENERIC NETLINK
 *
 * The API is exported to user space using generic netlink (other
 * methods can be added as needed).
 *
 * There is a Generic Netlink Family named "WiMAX", where interfaces
 * supporting the WiMAX interface receive commands and broadcast their
 * signals over a multicast group named "msg".
 *
 * Mapping to the source/destination interface is done by an interface
 * index attribute.
 *
 * For user-to-kernel traffic (commands) we use a function call
 * marshalling mechanism, where a message X with attributes A, B, C
 * sent from user space to kernel space means executing the WiMAX API
 * call wimax_X(A, B, C), sending the results back as a message.
 *
 * Kernel-to-user (notifications or signals) communication is sent
 * over multicast groups. This allows to have multiple applications
 * monitoring them.
 *
 * Each command/signal gets assigned it's own attribute policy. This
 * way the validator will verify that all the attributes in there are
 * only the ones that should be for each command/signal. Thing of an
 * attribute mapping to a type+argumentname for each command/signal.
 *
 * If we had a single policy for *all* commands/signals, after running
 * the validator we'd have to check "does this attribute belong in
 * here"?  for each one. It can be done manually, but it's just easier
 * to have the validator do that job with multiple policies. As well,
 * it makes it easier to later expand each command/signal signature
 * without affecting others and keeping the namespace more or less
 * sane. Not that it is too complicated, but it makes it even easier.
 *
 * No state information is maintained in the kernel for each user
 * space connection (the connection is stateless).
 *
 * TESTING FOR THE INTERFACE AND VERSIONING
 *
 * If network interface X is a WiMAX device, there will be a Generic
 * Netlink family named "WiMAX X" and the device will present a
 * "wimax" directory in it's network sysfs directory
 * (/sys/class/net/DEVICE/wimax) [used by HAL].
 *
 * The inexistence of any of these means the device does not support
 * this WiMAX API.
 *
 * By querying the generic netlink controller, versioning information
 * and the multicast groups available can be found. Applications using
 * the interface can either rely on that or use the generic netlink
 * controller to figure out which generic netlink commands/signals are
 * supported.
 *
 * NOTE: this versioning is a last resort to avoid hard
 *    incompatibilities. It is the intention of the design of this
 *    stack not to introduce backward incompatible changes.
 *
 * The version code has to fit in one byte (restrictions imposed by
 * generic netlink); we use `version / 10` for the major version and
 * `version % 10` for the minor. This gives 9 minors for each major
 * and 25 majors.
 *
 * The version change protocol is as follow:
 *
 * - Major versions: needs to be increased if an existing message/API
 *   call is changed or removed. Doesn't need to be changed if a new
 *   message is added.
 *
 * - Minor version: needs to be increased if new messages/API calls are
 *   being added or some other consideration that doesn't impact the
 *   user-kernel interface too much (like some kind of bug fix) and
 *   that is kind of left up in the air to common sense.
 *
 * User space code should not try to work if the major version it was
 * compiled for differs from what the kernel offers. As well, if the
 * minor version of the kernel interface is lower than the one user
 * space is expecting (the one it was compiled for), the kernel
 * might be missing API calls; user space shall be ready to handle
 * said condition. Use the generic netlink controller operations to
 * find which ones are supported and which not.
 *
 * libwimaxll:wimaxll_open() takes care of checking versions.
 *
 * THE OPERATIONS:
 *
 * Each operation is defined in its on file (drivers/net/wimax/op-*.c)
 * for clarity. The parts needed for an operation are:
 *
 *  - a function pointer in `struct wimax_dev`: optional, as the
 *    operation might be implemented by the stack and not by the
 *    driver.
 *
 *    All function pointers are named wimax_dev->op_*(), and drivers
 *    must implement them except where noted otherwise.
 *
 *  - When exported to user space, a `struct nla_policy` to define the
 *    attributes of the generic netlink command and a `struct genl_ops`
 *    to define the operation.
 *
 * All the declarations for the operation codes (WIMAX_GNL_OP_<NAME>)
 * and generic netlink attributes (WIMAX_GNL_<NAME>_*) are declared in
 * include/linux/wimax.h; this file is intended to be cloned by user
 * space to gain access to those declarations.
 *
 * A few caveats to remember:
 *
 *  - Need to define attribute numbers starting in 1; otherwise it
 *    fails.
 *
 *  - the `struct genl_family` requires a maximum attribute id; when
 *    defining the `struct nla_policy` for each message, it has to have
 *    an array size of WIMAX_GNL_ATTR_MAX+1.
 *
 * The op_*() function pointers will not be called if the wimax_dev is
 * in a state <= %WIMAX_ST_UNINITIALIZED. The exception is:
 *
 * - op_reset: can be called at any time after wimax_dev_add() has
 *   been called.
 *
 * THE PIPE INTERFACE:
 *
 * This interface is kept intentionally simple. The driver can send
 * and receive free-form messages to/from user space through a
 * pipe. See drivers/net/wimax/op-msg.c for details.
 *
 * The kernel-to-user messages are sent with
 * wimax_msg(). user-to-kernel messages are delivered via
 * wimax_dev->op_msg_from_user().
 *
 * RFKILL:
 *
 * RFKILL support is built into the wimax_dev layer; the driver just
 * needs to call wimax_report_rfkill_{hw,sw}() to inform of changes in
 * the hardware or software RF kill switches. When the stack wants to
 * turn the radio off, it will call wimax_dev->op_rfkill_sw_toggle(),
 * which the driver implements.
 *
 * User space can set the software RF Kill switch by calling
 * wimax_rfkill().
 *
 * The code for now only supports devices that don't require polling;
 * If the device needs to be polled, create a self-rearming delayed
 * work struct for polling or look into adding polled support to the
 * WiMAX stack.
 *
 * When initializing the hardware (_probe), after calling
 * wimax_dev_add(), query the device for it's RF Kill switches status
 * and feed it back to the WiMAX stack using
 * wimax_report_rfkill_{hw,sw}(). If any switch is missing, always
 * report it as ON.
 *
 * NOTE: the wimax stack uses an inverted terminology to that of the
 * RFKILL subsystem:
 *
 *  - ON: radio is ON, RFKILL is DISABLED or OFF.
 *  - OFF: radio is OFF, RFKILL is ENABLED or ON.
 *
 * MISCELLANEOUS OPS:
 *
 * wimax_reset() can be used to reset the device to power on state; by
 * default it issues a warm reset that maintains the same device
 * node. If that is not possible, it falls back to a cold reset
 * (device reconnect). The driver implements the backend to this
 * through wimax_dev->op_reset().
 */

#ifndef __NET__WIMAX_H__
#define __NET__WIMAX_H__

#include <linux/wimax.h>
#include <net/genetlink.h>
#include <linux/netdevice.h>

struct net_device;
struct genl_info;
struct wimax_dev;

/**
 * struct wimax_dev - Generic WiMAX device
 *
 * @net_dev: [fill] Pointer to the &struct net_device this WiMAX
 *     device implements.
 *
 * @op_msg_from_user: [fill] Driver-specific operation to
 *     handle a raw message from user space to the driver. The
 *     driver can send messages to user space using with
 *     wimax_msg_to_user().
 *
 * @op_rfkill_sw_toggle: [fill] Driver-specific operation to act on
 *     userspace (or any other agent) requesting the WiMAX device to
 *     change the RF Kill software switch (WIMAX_RF_ON or
 *     WIMAX_RF_OFF).
 *     If such hardware support is not present, it is assumed the
 *     radio cannot be switched off and it is always on (and the stack
 *     will error out when trying to switch it off). In such case,
 *     this function pointer can be left as NULL.
 *
 * @op_reset: [fill] Driver specific operation to reset the
 *     device.
 *     This operation should always attempt first a warm reset that
 *     does not disconnect the device from the bus and return 0.
 *     If that fails, it should resort to some sort of cold or bus
 *     reset (even if it implies a bus disconnection and device
 *     disappearance). In that case, -ENODEV should be returned to
 *     indicate the device is gone.
 *     This operation has to be synchronous, and return only when the
 *     reset is complete. In case of having had to resort to bus/cold
 *     reset implying a device disconnection, the call is allowed to
 *     return immediately.
 *     NOTE: wimax_dev->mutex is NOT locked when this op is being
 *     called; however, wimax_dev->mutex_reset IS locked to ensure
 *     serialization of calls to wimax_reset().
 *     See wimax_reset()'s documentation.
 *
 * @name: [fill] A way to identify this device. We need to register a
 *     name with many subsystems (rfkill, workqueue creation, etc).
 *     We can't use the network device name as that
 *     might change and in some instances we don't know it yet (until
 *     we don't call register_netdev()). So we generate an unique one
 *     using the driver name and device bus id, place it here and use
 *     it across the board. Recommended naming:
 *     DRIVERNAME-BUSNAME:BUSID (dev->bus->name, dev->bus_id).
 *
 * @id_table_node: [private] link to the list of wimax devices kept by
 *     id-table.c. Protected by it's own spinlock.
 *
 * @mutex: [private] Serializes all concurrent access and execution of
 *     operations.
 *
 * @mutex_reset: [private] Serializes reset operations. Needs to be a
 *     different mutex because as part of the reset operation, the
 *     driver has to call back into the stack to do things such as
 *     state change, that require wimax_dev->mutex.
 *
 * @state: [private] Current state of the WiMAX device.
 *
 * @rfkill: [private] integration into the RF-Kill infrastructure.
 *
 * @rf_sw: [private] State of the software radio switch (OFF/ON)
 *
 * @rf_hw: [private] State of the hardware radio switch (OFF/ON)
 *
 * @debugfs_dentry: [private] Used to hook up a debugfs entry. This
 *     shows up in the debugfs root as wimax\:DEVICENAME.
 *
 * Description:
 * This structure defines a common interface to access all WiMAX
 * devices from different vendors and provides a common API as well as
 * a free-form device-specific messaging channel.
 *
 * Usage:
 *  1. Embed a &struct wimax_dev at *the beginning* the network
 *     device structure so that netdev_priv() points to it.
 *
 *  2. memset() it to zero
 *
 *  3. Initialize with wimax_dev_init(). This will leave the WiMAX
 *     device in the %__WIMAX_ST_NULL state.
 *
 *  4. Fill all the fields marked with [fill]; once called
 *     wimax_dev_add(), those fields CANNOT be modified.
 *
 *  5. Call wimax_dev_add() *after* registering the network
 *     device. This will leave the WiMAX device in the %WIMAX_ST_DOWN
 *     state.
 *     Protect the driver's net_device->open() against succeeding if
 *     the wimax device state is lower than %WIMAX_ST_DOWN.
 *
 *  6. Select when the device is going to be turned on/initialized;
 *     for example, it could be initialized on 'ifconfig up' (when the
 *     netdev op 'open()' is called on the driver).
 *
 * When the device is initialized (at `ifconfig up` time, or right
 * after calling wimax_dev_add() from _probe(), make sure the
 * following steps are taken
 *
 *  a. Move the device to %WIMAX_ST_UNINITIALIZED. This is needed so
 *     some API calls that shouldn't work until the device is ready
 *     can be blocked.
 *
 *  b. Initialize the device. Make sure to turn the SW radio switch
 *     off and move the device to state %WIMAX_ST_RADIO_OFF when
 *     done. When just initialized, a device should be left in RADIO
 *     OFF state until user space devices to turn it on.
 *
 *  c. Query the device for the state of the hardware rfkill switch
 *     and call wimax_rfkill_report_hw() and wimax_rfkill_report_sw()
 *     as needed. See below.
 *
 * wimax_dev_rm() undoes before unregistering the network device. Once
 * wimax_dev_add() is called, the driver can get called on the
 * wimax_dev->op_* function pointers
 *
 * CONCURRENCY:
 *
 * The stack provides a mutex for each device that will disallow API
 * calls happening concurrently; thus, op calls into the driver
 * through the wimax_dev->op*() function pointers will always be
 * serialized and *never* concurrent.
 *
 * For locking, take wimax_dev->mutex is taken; (most) operations in
 * the API have to check for wimax_dev_is_ready() to return 0 before
 * continuing (this is done internally).
 *
 * REFERENCE COUNTING:
 *
 * The WiMAX device is reference counted by the associated network
 * device. The only operation that can be used to reference the device
 * is wimax_dev_get_by_genl_info(), and the reference it acquires has
 * to be released with dev_put(wimax_dev->net_dev).
 *
 * RFKILL:
 *
 * At startup, both HW and SW radio switchess are assumed to be off.
 *
 * At initialization time [after calling wimax_dev_add()], have the
 * driver query the device for the status of the software and hardware
 * RF kill switches and call wimax_report_rfkill_hw() and
 * wimax_rfkill_report_sw() to indicate their state. If any is
 * missing, just call it to indicate it is ON (radio always on).
 *
 * Whenever the driver detects a change in the state of the RF kill
 * switches, it should call wimax_report_rfkill_hw() or
 * wimax_report_rfkill_sw() to report it to the stack.
 */
struct wimax_dev {
	struct net_device *net_dev;
	struct list_head id_table_node;
	struct mutex mutex;		/* Protects all members and API calls */
	struct mutex mutex_reset;
	enum wimax_st state;

	int (*op_msg_from_user)(struct wimax_dev *wimax_dev,
				const char *,
				const void *, size_t,
				const struct genl_info *info);
	int (*op_rfkill_sw_toggle)(struct wimax_dev *wimax_dev,
				   enum wimax_rf_state);
	int (*op_reset)(struct wimax_dev *wimax_dev);

	struct rfkill *rfkill;
	unsigned int rf_hw;
	unsigned int rf_sw;
	char name[32];

	struct dentry *debugfs_dentry;
};



/*
 * WiMAX stack public API for device drivers
 * -----------------------------------------
 *
 * These functions are not exported to user space.
 */
void wimax_dev_init(struct wimax_dev *);
int wimax_dev_add(struct wimax_dev *, struct net_device *);
void wimax_dev_rm(struct wimax_dev *);

static inline
struct wimax_dev *net_dev_to_wimax(struct net_device *net_dev)
{
	return netdev_priv(net_dev);
}

static inline
struct device *wimax_dev_to_dev(struct wimax_dev *wimax_dev)
{
	return wimax_dev->net_dev->dev.parent;
}

void wimax_state_change(struct wimax_dev *, enum wimax_st);
enum wimax_st wimax_state_get(struct wimax_dev *);

/*
 * Radio Switch state reporting.
 *
 * enum wimax_rf_state is declared in linux/wimax.h so the exports
 * to user space can use it.
 */
void wimax_report_rfkill_hw(struct wimax_dev *, enum wimax_rf_state);
void wimax_report_rfkill_sw(struct wimax_dev *, enum wimax_rf_state);


/*
 * Free-form messaging to/from user space
 *
 * Sending a message:
 *
 *   wimax_msg(wimax_dev, pipe_name, buf, buf_size, GFP_KERNEL);
 *
 * Broken up:
 *
 *   skb = wimax_msg_alloc(wimax_dev, pipe_name, buf_size, GFP_KERNEL);
 *   ...fill up skb...
 *   wimax_msg_send(wimax_dev, pipe_name, skb);
 *
 * Be sure not to modify skb->data in the middle (ie: don't use
 * skb_push()/skb_pull()/skb_reserve() on the skb).
 *
 * "pipe_name" is any string, that can be interpreted as the name of
 * the pipe or recipient; the interpretation of it is driver
 * specific, so the recipient can multiplex it as wished. It can be
 * NULL, it won't be used - an example is using a "diagnostics" tag to
 * send diagnostics information that a device-specific diagnostics
 * tool would be interested in.
 */
struct sk_buff *wimax_msg_alloc(struct wimax_dev *, const char *, const void *,
				size_t, gfp_t);
int wimax_msg_send(struct wimax_dev *, struct sk_buff *);
int wimax_msg(struct wimax_dev *, const char *, const void *, size_t, gfp_t);

const void *wimax_msg_data_len(struct sk_buff *, size_t *);
const void *wimax_msg_data(struct sk_buff *);
ssize_t wimax_msg_len(struct sk_buff *);


/*
 * WiMAX stack user space API
 * --------------------------
 *
 * This API is what gets exported to user space for general
 * operations. As well, they can be called from within the kernel,
 * (with a properly referenced `struct wimax_dev`).
 *
 * Properly referenced means: the 'struct net_device' that embeds the
 * device's control structure and (as such) the 'struct wimax_dev' is
 * referenced by the caller.
 */
int wimax_rfkill(struct wimax_dev *, enum wimax_rf_state);
int wimax_reset(struct wimax_dev *);

#endif /* #ifndef __NET__WIMAX_H__ */
