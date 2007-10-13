/* ------------------------------------------------------------------------- */
/*									     */
/* i2c.h - definitions for the i2c-bus interface			     */
/*									     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-2000 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and
   Frodo Looijaard <frodol@dds.nl> */

#ifndef _LINUX_I2C_H
#define _LINUX_I2C_H

#include <linux/types.h>
#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/i2c-id.h>
#include <linux/mod_devicetable.h>
#include <linux/device.h>	/* for struct device */
#include <linux/sched.h>	/* for completion */
#include <linux/mutex.h>

/* --- General options ------------------------------------------------	*/

struct i2c_msg;
struct i2c_algorithm;
struct i2c_adapter;
struct i2c_client;
struct i2c_driver;
union i2c_smbus_data;

/*
 * The master routines are the ones normally used to transmit data to devices
 * on a bus (or read from them). Apart from two basic transfer functions to
 * transmit one message at a time, a more complex version can be used to
 * transmit an arbitrary number of messages without interruption.
 */
extern int i2c_master_send(struct i2c_client *,const char* ,int);
extern int i2c_master_recv(struct i2c_client *,char* ,int);

/* Transfer num messages.
 */
extern int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);


/* This is the very generalized SMBus access routine. You probably do not
   want to use this, though; one of the functions below may be much easier,
   and probably just as fast.
   Note that we use i2c_adapter here, because you do not need a specific
   smbus adapter to call this function. */
extern s32 i2c_smbus_xfer (struct i2c_adapter * adapter, u16 addr,
                           unsigned short flags,
                           char read_write, u8 command, int size,
                           union i2c_smbus_data * data);

/* Now follow the 'nice' access routines. These also document the calling
   conventions of smbus_access. */

extern s32 i2c_smbus_write_quick(struct i2c_client * client, u8 value);
extern s32 i2c_smbus_read_byte(struct i2c_client * client);
extern s32 i2c_smbus_write_byte(struct i2c_client * client, u8 value);
extern s32 i2c_smbus_read_byte_data(struct i2c_client * client, u8 command);
extern s32 i2c_smbus_write_byte_data(struct i2c_client * client,
                                     u8 command, u8 value);
extern s32 i2c_smbus_read_word_data(struct i2c_client * client, u8 command);
extern s32 i2c_smbus_write_word_data(struct i2c_client * client,
                                     u8 command, u16 value);
/* Returns the number of read bytes */
extern s32 i2c_smbus_read_block_data(struct i2c_client *client,
				     u8 command, u8 *values);
extern s32 i2c_smbus_write_block_data(struct i2c_client * client,
				      u8 command, u8 length,
				      const u8 *values);
/* Returns the number of read bytes */
extern s32 i2c_smbus_read_i2c_block_data(struct i2c_client * client,
					 u8 command, u8 length, u8 *values);
extern s32 i2c_smbus_write_i2c_block_data(struct i2c_client * client,
					  u8 command, u8 length,
					  const u8 *values);

/*
 * A driver is capable of handling one or more physical devices present on
 * I2C adapters. This information is used to inform the driver of adapter
 * events.
 *
 * The driver.owner field should be set to the module owner of this driver.
 * The driver.name field should be set to the name of this driver.
 */

struct i2c_driver {
	int id;
	unsigned int class;

	/* Notifies the driver that a new bus has appeared. This routine
	 * can be used by the driver to test if the bus meets its conditions
	 * & seek for the presence of the chip(s) it supports. If found, it
	 * registers the client(s) that are on the bus to the i2c admin. via
	 * i2c_attach_client.  (LEGACY I2C DRIVERS ONLY)
	 */
	int (*attach_adapter)(struct i2c_adapter *);
	int (*detach_adapter)(struct i2c_adapter *);

	/* tells the driver that a client is about to be deleted & gives it
	 * the chance to remove its private data. Also, if the client struct
	 * has been dynamically allocated by the driver in the function above,
	 * it must be freed here.  (LEGACY I2C DRIVERS ONLY)
	 */
	int (*detach_client)(struct i2c_client *);

	/* Standard driver model interfaces, for "new style" i2c drivers.
	 * With the driver model, device enumeration is NEVER done by drivers;
	 * it's done by infrastructure.  (NEW STYLE DRIVERS ONLY)
	 */
	int (*probe)(struct i2c_client *);
	int (*remove)(struct i2c_client *);

	/* driver model interfaces that don't relate to enumeration  */
	void (*shutdown)(struct i2c_client *);
	int (*suspend)(struct i2c_client *, pm_message_t mesg);
	int (*resume)(struct i2c_client *);

	/* a ioctl like command that can be used to perform specific functions
	 * with the device.
	 */
	int (*command)(struct i2c_client *client,unsigned int cmd, void *arg);

	struct device_driver driver;
	struct list_head list;
};
#define to_i2c_driver(d) container_of(d, struct i2c_driver, driver)

#define I2C_NAME_SIZE	20

/**
 * struct i2c_client - represent an I2C slave device
 * @flags: I2C_CLIENT_TEN indicates the device uses a ten bit chip address;
 *	I2C_CLIENT_PEC indicates it uses SMBus Packet Error Checking
 * @addr: Address used on the I2C bus connected to the parent adapter.
 * @name: Indicates the type of the device, usually a chip name that's
 *	generic enough to hide second-sourcing and compatible revisions.
 * @adapter: manages the bus segment hosting this I2C device
 * @driver: device's driver, hence pointer to access routines
 * @usage_count: counts current number of users of this client
 * @dev: Driver model device node for the slave.
 * @irq: indicates the IRQ generated by this device (if any)
 * @driver_name: Identifies new-style driver used with this device; also
 *	used as the module name for hotplug/coldplug modprobe support.
 * @list: list of active/busy clients
 * @released: used to synchronize client releases & detaches and references
 *
 * An i2c_client identifies a single device (i.e. chip) connected to an
 * i2c bus. The behaviour exposed to Linux is defined by the driver
 * managing the device.
 */
struct i2c_client {
	unsigned short flags;		/* div., see below		*/
	unsigned short addr;		/* chip address - NOTE: 7bit	*/
					/* addresses are stored in the	*/
					/* _LOWER_ 7 bits		*/
	char name[I2C_NAME_SIZE];
	struct i2c_adapter *adapter;	/* the adapter we sit on	*/
	struct i2c_driver *driver;	/* and our access routines	*/
	int usage_count;		/* How many accesses currently  */
					/* to the client		*/
	struct device dev;		/* the device structure		*/
	int irq;			/* irq issued by device (or -1) */
	char driver_name[KOBJ_NAME_LEN];
	struct list_head list;
	struct completion released;
};
#define to_i2c_client(d) container_of(d, struct i2c_client, dev)

static inline struct i2c_client *kobj_to_i2c_client(struct kobject *kobj)
{
	struct device * const dev = container_of(kobj, struct device, kobj);
	return to_i2c_client(dev);
}

static inline void *i2c_get_clientdata (struct i2c_client *dev)
{
	return dev_get_drvdata (&dev->dev);
}

static inline void i2c_set_clientdata (struct i2c_client *dev, void *data)
{
	dev_set_drvdata (&dev->dev, data);
}

/**
 * struct i2c_board_info - template for device creation
 * @driver_name: identifies the driver to be bound to the device
 * @type: optional chip type information, to initialize i2c_client.name
 * @flags: to initialize i2c_client.flags
 * @addr: stored in i2c_client.addr
 * @platform_data: stored in i2c_client.dev.platform_data
 * @irq: stored in i2c_client.irq
 *
 * I2C doesn't actually support hardware probing, although controllers and
 * devices may be able to use I2C_SMBUS_QUICK to tell whether or not there's
 * a device at a given address.  Drivers commonly need more information than
 * that, such as chip type, configuration, associated IRQ, and so on.
 *
 * i2c_board_info is used to build tables of information listing I2C devices
 * that are present.  This information is used to grow the driver model tree
 * for "new style" I2C drivers.  For mainboards this is done statically using
 * i2c_register_board_info(); bus numbers identify adapters that aren't
 * yet available.  For add-on boards, i2c_new_device() does this dynamically
 * with the adapter already known.
 */
struct i2c_board_info {
	char		driver_name[KOBJ_NAME_LEN];
	char		type[I2C_NAME_SIZE];
	unsigned short	flags;
	unsigned short	addr;
	void		*platform_data;
	int		irq;
};

/**
 * I2C_BOARD_INFO - macro used to list an i2c device and its driver
 * @driver: identifies the driver to use with the device
 * @dev_addr: the device's address on the bus.
 *
 * This macro initializes essential fields of a struct i2c_board_info,
 * declaring what has been provided on a particular board.  Optional
 * fields (such as the chip type, its associated irq, or device-specific
 * platform_data) are provided using conventional syntax.
 */
#define I2C_BOARD_INFO(driver,dev_addr) \
	.driver_name = (driver), .addr = (dev_addr)


/* Add-on boards should register/unregister their devices; e.g. a board
 * with integrated I2C, a config eeprom, sensors, and a codec that's
 * used in conjunction with the primary hardware.
 */
extern struct i2c_client *
i2c_new_device(struct i2c_adapter *adap, struct i2c_board_info const *info);

/* If you don't know the exact address of an I2C device, use this variant
 * instead, which can probe for device presence in a list of possible
 * addresses.
 */
extern struct i2c_client *
i2c_new_probed_device(struct i2c_adapter *adap,
		      struct i2c_board_info *info,
		      unsigned short const *addr_list);

extern void i2c_unregister_device(struct i2c_client *);

/* Mainboard arch_initcall() code should register all its I2C devices.
 * This is done at arch_initcall time, before declaring any i2c adapters.
 * Modules for add-on boards must use other calls.
 */
extern int
i2c_register_board_info(int busnum, struct i2c_board_info const *info, unsigned n);


/*
 * The following structs are for those who like to implement new bus drivers:
 * i2c_algorithm is the interface to a class of hardware solutions which can
 * be addressed using the same bus algorithms - i.e. bit-banging or the PCF8584
 * to name two of the most common.
 */
struct i2c_algorithm {
	/* If an adapter algorithm can't do I2C-level access, set master_xfer
	   to NULL. If an adapter algorithm can do SMBus access, set
	   smbus_xfer. If set to NULL, the SMBus protocol is simulated
	   using common I2C messages */
	/* master_xfer should return the number of messages successfully
	   processed, or a negative value on error */
	int (*master_xfer)(struct i2c_adapter *adap,struct i2c_msg *msgs,
	                   int num);
	int (*smbus_xfer) (struct i2c_adapter *adap, u16 addr,
	                   unsigned short flags, char read_write,
	                   u8 command, int size, union i2c_smbus_data * data);

	/* To determine what the adapter supports */
	u32 (*functionality) (struct i2c_adapter *);
};

/*
 * i2c_adapter is the structure used to identify a physical i2c bus along
 * with the access algorithms necessary to access it.
 */
struct i2c_adapter {
	struct module *owner;
	unsigned int id;
	unsigned int class;
	const struct i2c_algorithm *algo; /* the algorithm to access the bus */
	void *algo_data;

	/* --- administration stuff. */
	int (*client_register)(struct i2c_client *);
	int (*client_unregister)(struct i2c_client *);

	/* data fields that are valid for all devices	*/
	u8 level; 			/* nesting level for lockdep */
	struct mutex bus_lock;
	struct mutex clist_lock;

	int timeout;
	int retries;
	struct device dev;		/* the adapter device */

	int nr;
	struct list_head clients;
	struct list_head list;
	char name[48];
	struct completion dev_released;
};
#define to_i2c_adapter(d) container_of(d, struct i2c_adapter, dev)

static inline void *i2c_get_adapdata (struct i2c_adapter *dev)
{
	return dev_get_drvdata (&dev->dev);
}

static inline void i2c_set_adapdata (struct i2c_adapter *dev, void *data)
{
	dev_set_drvdata (&dev->dev, data);
}

/*flags for the client struct: */
#define I2C_CLIENT_PEC	0x04		/* Use Packet Error Checking */
#define I2C_CLIENT_TEN	0x10		/* we have a ten bit chip address */
					/* Must equal I2C_M_TEN below */
#define I2C_CLIENT_WAKE	0x80		/* for board_info; true iff can wake */

/* i2c adapter classes (bitmask) */
#define I2C_CLASS_HWMON		(1<<0)	/* lm_sensors, ... */
#define I2C_CLASS_TV_ANALOG	(1<<1)	/* bttv + friends */
#define I2C_CLASS_TV_DIGITAL	(1<<2)	/* dvb cards */
#define I2C_CLASS_DDC		(1<<3)	/* i2c-matroxfb ? */
#define I2C_CLASS_CAM_ANALOG	(1<<4)	/* camera with analog CCD */
#define I2C_CLASS_CAM_DIGITAL	(1<<5)	/* most webcams */
#define I2C_CLASS_SOUND		(1<<6)	/* sound devices */
#define I2C_CLASS_ALL		(UINT_MAX) /* all of the above */

/* i2c_client_address_data is the struct for holding default client
 * addresses for a driver and for the parameters supplied on the
 * command line
 */
struct i2c_client_address_data {
	unsigned short *normal_i2c;
	unsigned short *probe;
	unsigned short *ignore;
	unsigned short **forces;
};

/* Internal numbers to terminate lists */
#define I2C_CLIENT_END		0xfffeU

/* The numbers to use to set I2C bus address */
#define ANY_I2C_BUS		0xffff


/* ----- functions exported by i2c.o */

/* administration...
 */
extern int i2c_add_adapter(struct i2c_adapter *);
extern int i2c_del_adapter(struct i2c_adapter *);
extern int i2c_add_numbered_adapter(struct i2c_adapter *);

extern int i2c_register_driver(struct module *, struct i2c_driver *);
extern void i2c_del_driver(struct i2c_driver *);

static inline int i2c_add_driver(struct i2c_driver *driver)
{
	return i2c_register_driver(THIS_MODULE, driver);
}

extern int i2c_attach_client(struct i2c_client *);
extern int i2c_detach_client(struct i2c_client *);

/* Should be used to make sure that client-struct is valid and that it
   is okay to access the i2c-client.
   returns -ENODEV if client has gone in the meantime */
extern int i2c_use_client(struct i2c_client *);
extern int i2c_release_client(struct i2c_client *);

/* call the i2c_client->command() of all attached clients with
 * the given arguments */
extern void i2c_clients_command(struct i2c_adapter *adap,
				unsigned int cmd, void *arg);

/* returns -EBUSY if address has been taken, 0 if not. Note that the only
   other place at which this is called is within i2c_attach_client; so
   you can cheat by simply not registering. Not recommended, of course! */
extern int i2c_check_addr (struct i2c_adapter *adapter, int addr);

/* Detect function. It iterates over all possible addresses itself.
 * It will only call found_proc if some client is connected at the
 * specific address (unless a 'force' matched);
 */
extern int i2c_probe(struct i2c_adapter *adapter,
		struct i2c_client_address_data *address_data,
		int (*found_proc) (struct i2c_adapter *, int, int));

extern struct i2c_adapter* i2c_get_adapter(int id);
extern void i2c_put_adapter(struct i2c_adapter *adap);


/* Return the functionality mask */
static inline u32 i2c_get_functionality(struct i2c_adapter *adap)
{
	return adap->algo->functionality(adap);
}

/* Return 1 if adapter supports everything we need, 0 if not. */
static inline int i2c_check_functionality(struct i2c_adapter *adap, u32 func)
{
	return (func & i2c_get_functionality(adap)) == func;
}

/* Return id number for a specific adapter */
static inline int i2c_adapter_id(struct i2c_adapter *adap)
{
	return adap->nr;
}
#endif /* __KERNEL__ */

/**
 * struct i2c_msg - an I2C transaction segment beginning with START
 * @addr: Slave address, either seven or ten bits.  When this is a ten
 *	bit address, I2C_M_TEN must be set in @flags and the adapter
 *	must support I2C_FUNC_10BIT_ADDR.
 * @flags: I2C_M_RD is handled by all adapters.  No other flags may be
 *	provided unless the adapter exported the relevant I2C_FUNC_*
 *	flags through i2c_check_functionality().
 * @len: Number of data bytes in @buf being read from or written to the
 *	I2C slave address.  For read transactions where I2C_M_RECV_LEN
 *	is set, the caller guarantees that this buffer can hold up to
 *	32 bytes in addition to the initial length byte sent by the
 *	slave (plus, if used, the SMBus PEC); and this value will be
 *	incremented by the number of block data bytes received.
 * @buf: The buffer into which data is read, or from which it's written.
 *
 * An i2c_msg is the low level representation of one segment of an I2C
 * transaction.  It is visible to drivers in the @i2c_transfer() procedure,
 * to userspace from i2c-dev, and to I2C adapter drivers through the
 * @i2c_adapter.@master_xfer() method.
 *
 * Except when I2C "protocol mangling" is used, all I2C adapters implement
 * the standard rules for I2C transactions.  Each transaction begins with a
 * START.  That is followed by the slave address, and a bit encoding read
 * versus write.  Then follow all the data bytes, possibly including a byte
 * with SMBus PEC.  The transfer terminates with a NAK, or when all those
 * bytes have been transferred and ACKed.  If this is the last message in a
 * group, it is followed by a STOP.  Otherwise it is followed by the next
 * @i2c_msg transaction segment, beginning with a (repeated) START.
 *
 * Alternatively, when the adapter supports I2C_FUNC_PROTOCOL_MANGLING then
 * passing certain @flags may have changed those standard protocol behaviors.
 * Those flags are only for use with broken/nonconforming slaves, and with
 * adapters which are known to support the specific mangling options they
 * need (one or more of IGNORE_NAK, NO_RD_ACK, NOSTART, and REV_DIR_ADDR).
 */
struct i2c_msg {
	__u16 addr;	/* slave address			*/
	__u16 flags;
#define I2C_M_TEN		0x0010	/* this is a ten bit chip address */
#define I2C_M_RD		0x0001	/* read data, from slave to master */
#define I2C_M_NOSTART		0x4000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_REV_DIR_ADDR	0x2000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_IGNORE_NAK	0x1000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_NO_RD_ACK		0x0800	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_RECV_LEN		0x0400	/* length will be first received byte */
	__u16 len;		/* msg length				*/
	__u8 *buf;		/* pointer to msg data			*/
};

/* To determine what functionality is present */

#define I2C_FUNC_I2C			0x00000001
#define I2C_FUNC_10BIT_ADDR		0x00000002
#define I2C_FUNC_PROTOCOL_MANGLING	0x00000004 /* I2C_M_{REV_DIR_ADDR,NOSTART,..} */
#define I2C_FUNC_SMBUS_HWPEC_CALC	0x00000008 /* SMBus 2.0 */
#define I2C_FUNC_SMBUS_BLOCK_PROC_CALL	0x00008000 /* SMBus 2.0 */
#define I2C_FUNC_SMBUS_QUICK		0x00010000
#define I2C_FUNC_SMBUS_READ_BYTE	0x00020000
#define I2C_FUNC_SMBUS_WRITE_BYTE	0x00040000
#define I2C_FUNC_SMBUS_READ_BYTE_DATA	0x00080000
#define I2C_FUNC_SMBUS_WRITE_BYTE_DATA	0x00100000
#define I2C_FUNC_SMBUS_READ_WORD_DATA	0x00200000
#define I2C_FUNC_SMBUS_WRITE_WORD_DATA	0x00400000
#define I2C_FUNC_SMBUS_PROC_CALL	0x00800000
#define I2C_FUNC_SMBUS_READ_BLOCK_DATA	0x01000000
#define I2C_FUNC_SMBUS_WRITE_BLOCK_DATA 0x02000000
#define I2C_FUNC_SMBUS_READ_I2C_BLOCK	0x04000000 /* I2C-like block xfer  */
#define I2C_FUNC_SMBUS_WRITE_I2C_BLOCK	0x08000000 /* w/ 1-byte reg. addr. */
#define I2C_FUNC_SMBUS_READ_I2C_BLOCK_2	 0x10000000 /* I2C-like block xfer  */
#define I2C_FUNC_SMBUS_WRITE_I2C_BLOCK_2 0x20000000 /* w/ 2-byte reg. addr. */

#define I2C_FUNC_SMBUS_BYTE (I2C_FUNC_SMBUS_READ_BYTE | \
                             I2C_FUNC_SMBUS_WRITE_BYTE)
#define I2C_FUNC_SMBUS_BYTE_DATA (I2C_FUNC_SMBUS_READ_BYTE_DATA | \
                                  I2C_FUNC_SMBUS_WRITE_BYTE_DATA)
#define I2C_FUNC_SMBUS_WORD_DATA (I2C_FUNC_SMBUS_READ_WORD_DATA | \
                                  I2C_FUNC_SMBUS_WRITE_WORD_DATA)
#define I2C_FUNC_SMBUS_BLOCK_DATA (I2C_FUNC_SMBUS_READ_BLOCK_DATA | \
                                   I2C_FUNC_SMBUS_WRITE_BLOCK_DATA)
#define I2C_FUNC_SMBUS_I2C_BLOCK (I2C_FUNC_SMBUS_READ_I2C_BLOCK | \
                                  I2C_FUNC_SMBUS_WRITE_I2C_BLOCK)
#define I2C_FUNC_SMBUS_I2C_BLOCK_2 (I2C_FUNC_SMBUS_READ_I2C_BLOCK_2 | \
                                    I2C_FUNC_SMBUS_WRITE_I2C_BLOCK_2)

#define I2C_FUNC_SMBUS_EMUL (I2C_FUNC_SMBUS_QUICK | \
                             I2C_FUNC_SMBUS_BYTE | \
                             I2C_FUNC_SMBUS_BYTE_DATA | \
                             I2C_FUNC_SMBUS_WORD_DATA | \
                             I2C_FUNC_SMBUS_PROC_CALL | \
                             I2C_FUNC_SMBUS_WRITE_BLOCK_DATA | \
                             I2C_FUNC_SMBUS_I2C_BLOCK)

/*
 * Data for SMBus Messages
 */
#define I2C_SMBUS_BLOCK_MAX	32	/* As specified in SMBus standard */
union i2c_smbus_data {
	__u8 byte;
	__u16 word;
	__u8 block[I2C_SMBUS_BLOCK_MAX + 2]; /* block[0] is used for length */
	                       /* and one more for user-space compatibility */
};

/* smbus_access read or write markers */
#define I2C_SMBUS_READ	1
#define I2C_SMBUS_WRITE	0

/* SMBus transaction types (size parameter in the above functions)
   Note: these no longer correspond to the (arbitrary) PIIX4 internal codes! */
#define I2C_SMBUS_QUICK		    0
#define I2C_SMBUS_BYTE		    1
#define I2C_SMBUS_BYTE_DATA	    2
#define I2C_SMBUS_WORD_DATA	    3
#define I2C_SMBUS_PROC_CALL	    4
#define I2C_SMBUS_BLOCK_DATA	    5
#define I2C_SMBUS_I2C_BLOCK_BROKEN  6
#define I2C_SMBUS_BLOCK_PROC_CALL   7		/* SMBus 2.0 */
#define I2C_SMBUS_I2C_BLOCK_DATA    8


/* ----- commands for the ioctl like i2c_command call:
 * note that additional calls are defined in the algorithm and hw
 *	dependent layers - these can be listed here, or see the
 *	corresponding header files.
 */
				/* -> bit-adapter specific ioctls	*/
#define I2C_RETRIES	0x0701	/* number of times a device address      */
				/* should be polled when not            */
                                /* acknowledging			*/
#define I2C_TIMEOUT	0x0702	/* set timeout - call with int		*/


/* this is for i2c-dev.c	*/
#define I2C_SLAVE	0x0703	/* Change slave address			*/
				/* Attn.: Slave address is 7 or 10 bits */
#define I2C_SLAVE_FORCE	0x0706	/* Change slave address			*/
				/* Attn.: Slave address is 7 or 10 bits */
				/* This changes the address, even if it */
				/* is already taken!			*/
#define I2C_TENBIT	0x0704	/* 0 for 7 bit addrs, != 0 for 10 bit	*/

#define I2C_FUNCS	0x0705	/* Get the adapter functionality */
#define I2C_RDWR	0x0707	/* Combined R/W transfer (one stop only)*/
#define I2C_PEC		0x0708	/* != 0 for SMBus PEC                   */

#define I2C_SMBUS	0x0720	/* SMBus-level access */

/* ----- I2C-DEV: char device interface stuff ------------------------- */
#ifdef __KERNEL__

#define I2C_MAJOR	89		/* Device major number		*/

/* These defines are used for probing i2c client addresses */
/* The length of the option lists */
#define I2C_CLIENT_MAX_OPTS 48

/* Default fill of many variables */
#define I2C_CLIENT_DEFAULTS {I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END, \
                          I2C_CLIENT_END, I2C_CLIENT_END, I2C_CLIENT_END}

/* I2C_CLIENT_MODULE_PARM creates a module parameter, and puts it in the
   module header */

#define I2C_CLIENT_MODULE_PARM(var,desc) \
  static unsigned short var[I2C_CLIENT_MAX_OPTS] = I2C_CLIENT_DEFAULTS; \
  static unsigned int var##_num; \
  module_param_array(var, short, &var##_num, 0); \
  MODULE_PARM_DESC(var,desc)

#define I2C_CLIENT_MODULE_PARM_FORCE(name)				\
I2C_CLIENT_MODULE_PARM(force_##name,					\
		       "List of adapter,address pairs which are "	\
		       "unquestionably assumed to contain a `"		\
		       # name "' chip")


#define I2C_CLIENT_INSMOD_COMMON					\
I2C_CLIENT_MODULE_PARM(probe, "List of adapter,address pairs to scan "	\
		       "additionally");					\
I2C_CLIENT_MODULE_PARM(ignore, "List of adapter,address pairs not to "	\
		       "scan");						\
static struct i2c_client_address_data addr_data = {			\
	.normal_i2c	= normal_i2c,					\
	.probe		= probe,					\
	.ignore		= ignore,					\
	.forces		= forces,					\
}

/* These are the ones you want to use in your own drivers. Pick the one
   which matches the number of devices the driver differenciates between. */
#define I2C_CLIENT_INSMOD \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
	static unsigned short *forces[] = {				\
			force,						\
			NULL						\
		};							\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_1(chip1)					\
enum chips { any_chip, chip1 };						\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
static unsigned short *forces[] = { force, force_##chip1, NULL };	\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_2(chip1, chip2)				\
enum chips { any_chip, chip1, chip2 };					\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip2);					\
static unsigned short *forces[] = { force, force_##chip1,		\
				    force_##chip2, NULL };		\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_3(chip1, chip2, chip3)			\
enum chips { any_chip, chip1, chip2, chip3 };				\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip2);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip3);					\
static unsigned short *forces[] = { force, force_##chip1,		\
				    force_##chip2, force_##chip3,	\
				    NULL };				\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_4(chip1, chip2, chip3, chip4)			\
enum chips { any_chip, chip1, chip2, chip3, chip4 };			\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip2);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip3);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip4);					\
static unsigned short *forces[] = { force, force_##chip1,		\
				    force_##chip2, force_##chip3,	\
				    force_##chip4, NULL};		\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_5(chip1, chip2, chip3, chip4, chip5)		\
enum chips { any_chip, chip1, chip2, chip3, chip4, chip5 };		\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip2);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip3);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip4);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip5);					\
static unsigned short *forces[] = { force, force_##chip1,		\
				    force_##chip2, force_##chip3,	\
				    force_##chip4, force_##chip5,	\
				    NULL };				\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_6(chip1, chip2, chip3, chip4, chip5, chip6)	\
enum chips { any_chip, chip1, chip2, chip3, chip4, chip5, chip6 };	\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip2);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip3);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip4);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip5);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip6);					\
static unsigned short *forces[] = { force, force_##chip1,		\
				    force_##chip2, force_##chip3,	\
				    force_##chip4, force_##chip5,	\
				    force_##chip6, NULL };		\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_7(chip1, chip2, chip3, chip4, chip5, chip6, chip7) \
enum chips { any_chip, chip1, chip2, chip3, chip4, chip5, chip6,	\
	     chip7 };							\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip2);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip3);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip4);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip5);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip6);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip7);					\
static unsigned short *forces[] = { force, force_##chip1,		\
				    force_##chip2, force_##chip3,	\
				    force_##chip4, force_##chip5,	\
				    force_##chip6, force_##chip7,	\
				    NULL };				\
I2C_CLIENT_INSMOD_COMMON

#define I2C_CLIENT_INSMOD_8(chip1, chip2, chip3, chip4, chip5, chip6, chip7, chip8) \
enum chips { any_chip, chip1, chip2, chip3, chip4, chip5, chip6,	\
	     chip7, chip8 };						\
I2C_CLIENT_MODULE_PARM(force, "List of adapter,address pairs to "	\
		       "boldly assume to be present");			\
I2C_CLIENT_MODULE_PARM_FORCE(chip1);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip2);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip3);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip4);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip5);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip6);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip7);					\
I2C_CLIENT_MODULE_PARM_FORCE(chip8);					\
static unsigned short *forces[] = { force, force_##chip1,		\
				    force_##chip2, force_##chip3,	\
				    force_##chip4, force_##chip5,	\
				    force_##chip6, force_##chip7,	\
				    force_##chip8, NULL };		\
I2C_CLIENT_INSMOD_COMMON
#endif /* __KERNEL__ */
#endif /* _LINUX_I2C_H */
