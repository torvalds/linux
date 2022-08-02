/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef I3C_MASTER_H
#define I3C_MASTER_H

#include <asm/bitsperlong.h>

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/i3c/ccc.h>
#include <linux/i3c/device.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define I3C_HOT_JOIN_ADDR		0x2
#define I3C_BROADCAST_ADDR		0x7e
#define I3C_MAX_ADDR			GENMASK(6, 0)

struct i3c_master_controller;
struct i3c_bus;
struct i2c_device;
struct i3c_device;

/**
 * struct i3c_i2c_dev_desc - Common part of the I3C/I2C device descriptor
 * @node: node element used to insert the slot into the I2C or I3C device
 *	  list
 * @master: I3C master that instantiated this device. Will be used to do
 *	    I2C/I3C transfers
 * @master_priv: master private data assigned to the device. Can be used to
 *		 add master specific information
 *
 * This structure is describing common I3C/I2C dev information.
 */
struct i3c_i2c_dev_desc {
	struct list_head node;
	struct i3c_master_controller *master;
	void *master_priv;
};

#define I3C_LVR_I2C_INDEX_MASK		GENMASK(7, 5)
#define I3C_LVR_I2C_INDEX(x)		((x) << 5)
#define I3C_LVR_I2C_FM_MODE		BIT(4)

#define I2C_MAX_ADDR			GENMASK(6, 0)

/**
 * struct i2c_dev_boardinfo - I2C device board information
 * @node: used to insert the boardinfo object in the I2C boardinfo list
 * @base: regular I2C board information
 * @lvr: LVR (Legacy Virtual Register) needed by the I3C core to know about
 *	 the I2C device limitations
 *
 * This structure is used to attach board-level information to an I2C device.
 * Each I2C device connected on the I3C bus should have one.
 */
struct i2c_dev_boardinfo {
	struct list_head node;
	struct i2c_board_info base;
	u8 lvr;
};

/**
 * struct i2c_dev_desc - I2C device descriptor
 * @common: common part of the I2C device descriptor
 * @boardinfo: pointer to the boardinfo attached to this I2C device
 * @dev: I2C device object registered to the I2C framework
 * @addr: I2C device address
 * @lvr: LVR (Legacy Virtual Register) needed by the I3C core to know about
 *	 the I2C device limitations
 *
 * Each I2C device connected on the bus will have an i2c_dev_desc.
 * This object is created by the core and later attached to the controller
 * using &struct_i3c_master_controller->ops->attach_i2c_dev().
 *
 * &struct_i2c_dev_desc is the internal representation of an I2C device
 * connected on an I3C bus. This object is also passed to all
 * &struct_i3c_master_controller_ops hooks.
 */
struct i2c_dev_desc {
	struct i3c_i2c_dev_desc common;
	struct i2c_client *dev;
	u16 addr;
	u8 lvr;
};

/**
 * struct i3c_ibi_slot - I3C IBI (In-Band Interrupt) slot
 * @work: work associated to this slot. The IBI handler will be called from
 *	  there
 * @dev: the I3C device that has generated this IBI
 * @len: length of the payload associated to this IBI
 * @data: payload buffer
 *
 * An IBI slot is an object pre-allocated by the controller and used when an
 * IBI comes in.
 * Every time an IBI comes in, the I3C master driver should find a free IBI
 * slot in its IBI slot pool, retrieve the IBI payload and queue the IBI using
 * i3c_master_queue_ibi().
 *
 * How IBI slots are allocated is left to the I3C master driver, though, for
 * simple kmalloc-based allocation, the generic IBI slot pool can be used.
 */
struct i3c_ibi_slot {
	struct work_struct work;
	struct i3c_dev_desc *dev;
	unsigned int len;
	void *data;
};

/**
 * struct i3c_device_ibi_info - IBI information attached to a specific device
 * @all_ibis_handled: used to be informed when no more IBIs are waiting to be
 *		      processed. Used by i3c_device_disable_ibi() to wait for
 *		      all IBIs to be dequeued
 * @pending_ibis: count the number of pending IBIs. Each pending IBI has its
 *		  work element queued to the controller workqueue
 * @max_payload_len: maximum payload length for an IBI coming from this device.
 *		     this value is specified when calling
 *		     i3c_device_request_ibi() and should not change at run
 *		     time. All messages IBIs exceeding this limit should be
 *		     rejected by the master
 * @num_slots: number of IBI slots reserved for this device
 * @enabled: reflect the IBI status
 * @handler: IBI handler specified at i3c_device_request_ibi() call time. This
 *	     handler will be called from the controller workqueue, and as such
 *	     is allowed to sleep (though it is recommended to process the IBI
 *	     as fast as possible to not stall processing of other IBIs queued
 *	     on the same workqueue).
 *	     New I3C messages can be sent from the IBI handler
 *
 * The &struct_i3c_device_ibi_info object is allocated when
 * i3c_device_request_ibi() is called and attached to a specific device. This
 * object is here to manage IBIs coming from a specific I3C device.
 *
 * Note that this structure is the generic view of the IBI management
 * infrastructure. I3C master drivers may have their own internal
 * representation which they can associate to the device using
 * controller-private data.
 */
struct i3c_device_ibi_info {
	struct completion all_ibis_handled;
	atomic_t pending_ibis;
	unsigned int max_payload_len;
	unsigned int num_slots;
	unsigned int enabled;
	void (*handler)(struct i3c_device *dev,
			const struct i3c_ibi_payload *payload);
};

/**
 * struct i3c_dev_boardinfo - I3C device board information
 * @node: used to insert the boardinfo object in the I3C boardinfo list
 * @init_dyn_addr: initial dynamic address requested by the FW. We provide no
 *		   guarantee that the device will end up using this address,
 *		   but try our best to assign this specific address to the
 *		   device
 * @static_addr: static address the I3C device listen on before it's been
 *		 assigned a dynamic address by the master. Will be used during
 *		 bus initialization to assign it a specific dynamic address
 *		 before starting DAA (Dynamic Address Assignment)
 * @pid: I3C Provisional ID exposed by the device. This is a unique identifier
 *	 that may be used to attach boardinfo to i3c_dev_desc when the device
 *	 does not have a static address
 * @of_node: optional DT node in case the device has been described in the DT
 *
 * This structure is used to attach board-level information to an I3C device.
 * Not all I3C devices connected on the bus will have a boardinfo. It's only
 * needed if you want to attach extra resources to a device or assign it a
 * specific dynamic address.
 */
struct i3c_dev_boardinfo {
	struct list_head node;
	u8 init_dyn_addr;
	u8 static_addr;
	u64 pid;
	struct device_node *of_node;
};

/**
 * struct i3c_dev_desc - I3C device descriptor
 * @common: common part of the I3C device descriptor
 * @info: I3C device information. Will be automatically filled when you create
 *	  your device with i3c_master_add_i3c_dev_locked()
 * @ibi_lock: lock used to protect the &struct_i3c_device->ibi
 * @ibi: IBI info attached to a device. Should be NULL until
 *	 i3c_device_request_ibi() is called
 * @dev: pointer to the I3C device object exposed to I3C device drivers. This
 *	 should never be accessed from I3C master controller drivers. Only core
 *	 code should manipulate it in when updating the dev <-> desc link or
 *	 when propagating IBI events to the driver
 * @boardinfo: pointer to the boardinfo attached to this I3C device
 *
 * Internal representation of an I3C device. This object is only used by the
 * core and passed to I3C master controller drivers when they're requested to
 * do some operations on the device.
 * The core maintains the link between the internal I3C dev descriptor and the
 * object exposed to the I3C device drivers (&struct_i3c_device).
 */
struct i3c_dev_desc {
	struct i3c_i2c_dev_desc common;
	struct i3c_device_info info;
	struct mutex ibi_lock;
	struct i3c_device_ibi_info *ibi;
	struct i3c_device *dev;
	const struct i3c_dev_boardinfo *boardinfo;
};

/**
 * struct i3c_device - I3C device object
 * @dev: device object to register the I3C dev to the device model
 * @desc: pointer to an i3c device descriptor object. This link is updated
 *	  every time the I3C device is rediscovered with a different dynamic
 *	  address assigned
 * @bus: I3C bus this device is attached to
 *
 * I3C device object exposed to I3C device drivers. The takes care of linking
 * this object to the relevant &struct_i3c_dev_desc one.
 * All I3C devs on the I3C bus are represented, including I3C masters. For each
 * of them, we have an instance of &struct i3c_device.
 */
struct i3c_device {
	struct device dev;
	struct i3c_dev_desc *desc;
	struct i3c_bus *bus;
};

/*
 * The I3C specification says the maximum number of devices connected on the
 * bus is 11, but this number depends on external parameters like trace length,
 * capacitive load per Device, and the types of Devices present on the Bus.
 * I3C master can also have limitations, so this number is just here as a
 * reference and should be adjusted on a per-controller/per-board basis.
 */
#define I3C_BUS_MAX_DEVS		11

#define I3C_BUS_MAX_I3C_SCL_RATE	12900000
#define I3C_BUS_TYP_I3C_SCL_RATE	12500000
#define I3C_BUS_I2C_FM_PLUS_SCL_RATE	1000000
#define I3C_BUS_I2C_FM_SCL_RATE		400000
#define I3C_BUS_TLOW_OD_MIN_NS		200

/**
 * enum i3c_bus_mode - I3C bus mode
 * @I3C_BUS_MODE_PURE: only I3C devices are connected to the bus. No limitation
 *		       expected
 * @I3C_BUS_MODE_MIXED_FAST: I2C devices with 50ns spike filter are present on
 *			     the bus. The only impact in this mode is that the
 *			     high SCL pulse has to stay below 50ns to trick I2C
 *			     devices when transmitting I3C frames
 * @I3C_BUS_MODE_MIXED_LIMITED: I2C devices without 50ns spike filter are
 *				present on the bus. However they allow
 *				compliance up to the maximum SDR SCL clock
 *				frequency.
 * @I3C_BUS_MODE_MIXED_SLOW: I2C devices without 50ns spike filter are present
 *			     on the bus
 */
enum i3c_bus_mode {
	I3C_BUS_MODE_PURE,
	I3C_BUS_MODE_MIXED_FAST,
	I3C_BUS_MODE_MIXED_LIMITED,
	I3C_BUS_MODE_MIXED_SLOW,
};

/**
 * enum i3c_addr_slot_status - I3C address slot status
 * @I3C_ADDR_SLOT_FREE: address is free
 * @I3C_ADDR_SLOT_RSVD: address is reserved
 * @I3C_ADDR_SLOT_I2C_DEV: address is assigned to an I2C device
 * @I3C_ADDR_SLOT_I3C_DEV: address is assigned to an I3C device
 * @I3C_ADDR_SLOT_STATUS_MASK: address slot mask
 *
 * On an I3C bus, addresses are assigned dynamically, and we need to know which
 * addresses are free to use and which ones are already assigned.
 *
 * Addresses marked as reserved are those reserved by the I3C protocol
 * (broadcast address, ...).
 */
enum i3c_addr_slot_status {
	I3C_ADDR_SLOT_FREE,
	I3C_ADDR_SLOT_RSVD,
	I3C_ADDR_SLOT_I2C_DEV,
	I3C_ADDR_SLOT_I3C_DEV,
	I3C_ADDR_SLOT_STATUS_MASK = 3,
};

/**
 * struct i3c_bus - I3C bus object
 * @cur_master: I3C master currently driving the bus. Since I3C is multi-master
 *		this can change over the time. Will be used to let a master
 *		know whether it needs to request bus ownership before sending
 *		a frame or not
 * @id: bus ID. Assigned by the framework when register the bus
 * @addrslots: a bitmap with 2-bits per-slot to encode the address status and
 *	       ease the DAA (Dynamic Address Assignment) procedure (see
 *	       &enum i3c_addr_slot_status)
 * @mode: bus mode (see &enum i3c_bus_mode)
 * @scl_rate.i3c: maximum rate for the clock signal when doing I3C SDR/priv
 *		  transfers
 * @scl_rate.i2c: maximum rate for the clock signal when doing I2C transfers
 * @scl_rate: SCL signal rate for I3C and I2C mode
 * @devs.i3c: contains a list of I3C device descriptors representing I3C
 *	      devices connected on the bus and successfully attached to the
 *	      I3C master
 * @devs.i2c: contains a list of I2C device descriptors representing I2C
 *	      devices connected on the bus and successfully attached to the
 *	      I3C master
 * @devs: 2 lists containing all I3C/I2C devices connected to the bus
 * @lock: read/write lock on the bus. This is needed to protect against
 *	  operations that have an impact on the whole bus and the devices
 *	  connected to it. For example, when asking slaves to drop their
 *	  dynamic address (RSTDAA CCC), we need to make sure no one is trying
 *	  to send I3C frames to these devices.
 *	  Note that this lock does not protect against concurrency between
 *	  devices: several drivers can send different I3C/I2C frames through
 *	  the same master in parallel. This is the responsibility of the
 *	  master to guarantee that frames are actually sent sequentially and
 *	  not interlaced
 *
 * The I3C bus is represented with its own object and not implicitly described
 * by the I3C master to cope with the multi-master functionality, where one bus
 * can be shared amongst several masters, each of them requesting bus ownership
 * when they need to.
 */
struct i3c_bus {
	struct i3c_dev_desc *cur_master;
	int id;
	unsigned long addrslots[((I2C_MAX_ADDR + 1) * 2) / BITS_PER_LONG];
	enum i3c_bus_mode mode;
	struct {
		unsigned long i3c;
		unsigned long i2c;
	} scl_rate;
	struct {
		struct list_head i3c;
		struct list_head i2c;
	} devs;
	struct rw_semaphore lock;
};

/**
 * struct i3c_master_controller_ops - I3C master methods
 * @bus_init: hook responsible for the I3C bus initialization. You should at
 *	      least call master_set_info() from there and set the bus mode.
 *	      You can also put controller specific initialization in there.
 *	      This method is mandatory.
 * @bus_cleanup: cleanup everything done in
 *		 &i3c_master_controller_ops->bus_init().
 *		 This method is optional.
 * @attach_i3c_dev: called every time an I3C device is attached to the bus. It
 *		    can be after a DAA or when a device is statically declared
 *		    by the FW, in which case it will only have a static address
 *		    and the dynamic address will be 0.
 *		    When this function is called, device information have not
 *		    been retrieved yet.
 *		    This is a good place to attach master controller specific
 *		    data to I3C devices.
 *		    This method is optional.
 * @reattach_i3c_dev: called every time an I3C device has its addressed
 *		      changed. It can be because the device has been powered
 *		      down and has lost its address, or it can happen when a
 *		      device had a static address and has been assigned a
 *		      dynamic address with SETDASA.
 *		      This method is optional.
 * @detach_i3c_dev: called when an I3C device is detached from the bus. Usually
 *		    happens when the master device is unregistered.
 *		    This method is optional.
 * @do_daa: do a DAA (Dynamic Address Assignment) procedure. This is procedure
 *	    should send an ENTDAA CCC command and then add all devices
 *	    discovered sure the DAA using i3c_master_add_i3c_dev_locked().
 *	    Add devices added with i3c_master_add_i3c_dev_locked() will then be
 *	    attached or re-attached to the controller.
 *	    This method is mandatory.
 * @supports_ccc_cmd: should return true if the CCC command is supported, false
 *		      otherwise.
 *		      This method is optional, if not provided the core assumes
 *		      all CCC commands are supported.
 * @send_ccc_cmd: send a CCC command
 *		  This method is mandatory.
 * @priv_xfers: do one or several private I3C SDR transfers
 *		This method is mandatory.
 * @attach_i2c_dev: called every time an I2C device is attached to the bus.
 *		    This is a good place to attach master controller specific
 *		    data to I2C devices.
 *		    This method is optional.
 * @detach_i2c_dev: called when an I2C device is detached from the bus. Usually
 *		    happens when the master device is unregistered.
 *		    This method is optional.
 * @i2c_xfers: do one or several I2C transfers. Note that, unlike i3c
 *	       transfers, the core does not guarantee that buffers attached to
 *	       the transfers are DMA-safe. If drivers want to have DMA-safe
 *	       buffers, they should use the i2c_get_dma_safe_msg_buf()
 *	       and i2c_put_dma_safe_msg_buf() helpers provided by the I2C
 *	       framework.
 *	       This method is mandatory.
 * @request_ibi: attach an IBI handler to an I3C device. This implies defining
 *		 an IBI handler and the constraints of the IBI (maximum payload
 *		 length and number of pre-allocated slots).
 *		 Some controllers support less IBI-capable devices than regular
 *		 devices, so this method might return -%EBUSY if there's no
 *		 more space for an extra IBI registration
 *		 This method is optional.
 * @free_ibi: free an IBI previously requested with ->request_ibi(). The IBI
 *	      should have been disabled with ->disable_irq() prior to that
 *	      This method is mandatory only if ->request_ibi is not NULL.
 * @enable_ibi: enable the IBI. Only valid if ->request_ibi() has been called
 *		prior to ->enable_ibi(). The controller should first enable
 *		the IBI on the controller end (for example, unmask the hardware
 *		IRQ) and then send the ENEC CCC command (with the IBI flag set)
 *		to the I3C device.
 *		This method is mandatory only if ->request_ibi is not NULL.
 * @disable_ibi: disable an IBI. First send the DISEC CCC command with the IBI
 *		 flag set and then deactivate the hardware IRQ on the
 *		 controller end.
 *		 This method is mandatory only if ->request_ibi is not NULL.
 * @recycle_ibi_slot: recycle an IBI slot. Called every time an IBI has been
 *		      processed by its handler. The IBI slot should be put back
 *		      in the IBI slot pool so that the controller can re-use it
 *		      for a future IBI
 *		      This method is mandatory only if ->request_ibi is not
 *		      NULL.
 */
struct i3c_master_controller_ops {
	int (*bus_init)(struct i3c_master_controller *master);
	void (*bus_cleanup)(struct i3c_master_controller *master);
	int (*attach_i3c_dev)(struct i3c_dev_desc *dev);
	int (*reattach_i3c_dev)(struct i3c_dev_desc *dev, u8 old_dyn_addr);
	void (*detach_i3c_dev)(struct i3c_dev_desc *dev);
	int (*do_daa)(struct i3c_master_controller *master);
	bool (*supports_ccc_cmd)(struct i3c_master_controller *master,
				 const struct i3c_ccc_cmd *cmd);
	int (*send_ccc_cmd)(struct i3c_master_controller *master,
			    struct i3c_ccc_cmd *cmd);
	int (*priv_xfers)(struct i3c_dev_desc *dev,
			  struct i3c_priv_xfer *xfers,
			  int nxfers);
	int (*attach_i2c_dev)(struct i2c_dev_desc *dev);
	void (*detach_i2c_dev)(struct i2c_dev_desc *dev);
	int (*i2c_xfers)(struct i2c_dev_desc *dev,
			 const struct i2c_msg *xfers, int nxfers);
	int (*request_ibi)(struct i3c_dev_desc *dev,
			   const struct i3c_ibi_setup *req);
	void (*free_ibi)(struct i3c_dev_desc *dev);
	int (*enable_ibi)(struct i3c_dev_desc *dev);
	int (*disable_ibi)(struct i3c_dev_desc *dev);
	void (*recycle_ibi_slot)(struct i3c_dev_desc *dev,
				 struct i3c_ibi_slot *slot);
};

/**
 * struct i3c_master_controller - I3C master controller object
 * @dev: device to be registered to the device-model
 * @this: an I3C device object representing this master. This device will be
 *	  added to the list of I3C devs available on the bus
 * @i2c: I2C adapter used for backward compatibility. This adapter is
 *	 registered to the I2C subsystem to be as transparent as possible to
 *	 existing I2C drivers
 * @ops: master operations. See &struct i3c_master_controller_ops
 * @secondary: true if the master is a secondary master
 * @init_done: true when the bus initialization is done
 * @boardinfo.i3c: list of I3C  boardinfo objects
 * @boardinfo.i2c: list of I2C boardinfo objects
 * @boardinfo: board-level information attached to devices connected on the bus
 * @bus: I3C bus exposed by this master
 * @wq: workqueue used to execute IBI handlers. Can also be used by master
 *	drivers if they need to postpone operations that need to take place
 *	in a thread context. Typical examples are Hot Join processing which
 *	requires taking the bus lock in maintenance, which in turn, can only
 *	be done from a sleep-able context
 *
 * A &struct i3c_master_controller has to be registered to the I3C subsystem
 * through i3c_master_register(). None of &struct i3c_master_controller fields
 * should be set manually, just pass appropriate values to
 * i3c_master_register().
 */
struct i3c_master_controller {
	struct device dev;
	struct i3c_dev_desc *this;
	struct i2c_adapter i2c;
	const struct i3c_master_controller_ops *ops;
	unsigned int secondary : 1;
	unsigned int init_done : 1;
	struct {
		struct list_head i3c;
		struct list_head i2c;
	} boardinfo;
	struct i3c_bus bus;
	struct workqueue_struct *wq;
};

/**
 * i3c_bus_for_each_i2cdev() - iterate over all I2C devices present on the bus
 * @bus: the I3C bus
 * @dev: an I2C device descriptor pointer updated to point to the current slot
 *	 at each iteration of the loop
 *
 * Iterate over all I2C devs present on the bus.
 */
#define i3c_bus_for_each_i2cdev(bus, dev)				\
	list_for_each_entry(dev, &(bus)->devs.i2c, common.node)

/**
 * i3c_bus_for_each_i3cdev() - iterate over all I3C devices present on the bus
 * @bus: the I3C bus
 * @dev: and I3C device descriptor pointer updated to point to the current slot
 *	 at each iteration of the loop
 *
 * Iterate over all I3C devs present on the bus.
 */
#define i3c_bus_for_each_i3cdev(bus, dev)				\
	list_for_each_entry(dev, &(bus)->devs.i3c, common.node)

int i3c_master_do_i2c_xfers(struct i3c_master_controller *master,
			    const struct i2c_msg *xfers,
			    int nxfers);

int i3c_master_disec_locked(struct i3c_master_controller *master, u8 addr,
			    u8 evts);
int i3c_master_enec_locked(struct i3c_master_controller *master, u8 addr,
			   u8 evts);
int i3c_master_entdaa_locked(struct i3c_master_controller *master);
int i3c_master_defslvs_locked(struct i3c_master_controller *master);

int i3c_master_get_free_addr(struct i3c_master_controller *master,
			     u8 start_addr);

int i3c_master_add_i3c_dev_locked(struct i3c_master_controller *master,
				  u8 addr);
int i3c_master_do_daa(struct i3c_master_controller *master);

int i3c_master_set_info(struct i3c_master_controller *master,
			const struct i3c_device_info *info);

int i3c_master_register(struct i3c_master_controller *master,
			struct device *parent,
			const struct i3c_master_controller_ops *ops,
			bool secondary);
int i3c_master_unregister(struct i3c_master_controller *master);

/**
 * i3c_dev_get_master_data() - get master private data attached to an I3C
 *			       device descriptor
 * @dev: the I3C device descriptor to get private data from
 *
 * Return: the private data previously attached with i3c_dev_set_master_data()
 *	   or NULL if no data has been attached to the device.
 */
static inline void *i3c_dev_get_master_data(const struct i3c_dev_desc *dev)
{
	return dev->common.master_priv;
}

/**
 * i3c_dev_set_master_data() - attach master private data to an I3C device
 *			       descriptor
 * @dev: the I3C device descriptor to attach private data to
 * @data: private data
 *
 * This functions allows a master controller to attach per-device private data
 * which can then be retrieved with i3c_dev_get_master_data().
 */
static inline void i3c_dev_set_master_data(struct i3c_dev_desc *dev,
					   void *data)
{
	dev->common.master_priv = data;
}

/**
 * i2c_dev_get_master_data() - get master private data attached to an I2C
 *			       device descriptor
 * @dev: the I2C device descriptor to get private data from
 *
 * Return: the private data previously attached with i2c_dev_set_master_data()
 *	   or NULL if no data has been attached to the device.
 */
static inline void *i2c_dev_get_master_data(const struct i2c_dev_desc *dev)
{
	return dev->common.master_priv;
}

/**
 * i2c_dev_set_master_data() - attach master private data to an I2C device
 *			       descriptor
 * @dev: the I2C device descriptor to attach private data to
 * @data: private data
 *
 * This functions allows a master controller to attach per-device private data
 * which can then be retrieved with i2c_device_get_master_data().
 */
static inline void i2c_dev_set_master_data(struct i2c_dev_desc *dev,
					   void *data)
{
	dev->common.master_priv = data;
}

/**
 * i3c_dev_get_master() - get master used to communicate with a device
 * @dev: I3C dev
 *
 * Return: the master controller driving @dev
 */
static inline struct i3c_master_controller *
i3c_dev_get_master(struct i3c_dev_desc *dev)
{
	return dev->common.master;
}

/**
 * i2c_dev_get_master() - get master used to communicate with a device
 * @dev: I2C dev
 *
 * Return: the master controller driving @dev
 */
static inline struct i3c_master_controller *
i2c_dev_get_master(struct i2c_dev_desc *dev)
{
	return dev->common.master;
}

/**
 * i3c_master_get_bus() - get the bus attached to a master
 * @master: master object
 *
 * Return: the I3C bus @master is connected to
 */
static inline struct i3c_bus *
i3c_master_get_bus(struct i3c_master_controller *master)
{
	return &master->bus;
}

struct i3c_generic_ibi_pool;

struct i3c_generic_ibi_pool *
i3c_generic_ibi_alloc_pool(struct i3c_dev_desc *dev,
			   const struct i3c_ibi_setup *req);
void i3c_generic_ibi_free_pool(struct i3c_generic_ibi_pool *pool);

struct i3c_ibi_slot *
i3c_generic_ibi_get_free_slot(struct i3c_generic_ibi_pool *pool);
void i3c_generic_ibi_recycle_slot(struct i3c_generic_ibi_pool *pool,
				  struct i3c_ibi_slot *slot);

void i3c_master_queue_ibi(struct i3c_dev_desc *dev, struct i3c_ibi_slot *slot);

struct i3c_ibi_slot *i3c_master_get_free_ibi_slot(struct i3c_dev_desc *dev);

#endif /* I3C_MASTER_H */
