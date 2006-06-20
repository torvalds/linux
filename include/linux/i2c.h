/* ------------------------------------------------------------------------- */
/* 									     */
/* i2c.h - definitions for the i2c-bus interface			     */
/* 									     */
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

/* --- For i2c-isa ---------------------------------------------------- */

extern void i2c_adapter_dev_release(struct device *dev);
extern struct device_driver i2c_adapter_driver;
extern struct class i2c_adapter_class;
extern struct bus_type i2c_bus_type;

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

/*
 * Some adapter types (i.e. PCF 8584 based ones) may support slave behaviuor. 
 * This is not tested/implemented yet and will change in the future.
 */
extern int i2c_slave_send(struct i2c_client *,char*,int);
extern int i2c_slave_recv(struct i2c_client *,char*,int);



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
extern s32 i2c_smbus_write_block_data(struct i2c_client * client,
				      u8 command, u8 length,
				      u8 *values);
/* Returns the number of read bytes */
extern s32 i2c_smbus_read_i2c_block_data(struct i2c_client * client,
					 u8 command, u8 *values);
extern s32 i2c_smbus_write_i2c_block_data(struct i2c_client * client,
					  u8 command, u8 length,
					  u8 *values);

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
	 * i2c_attach_client.
	 */
	int (*attach_adapter)(struct i2c_adapter *);
	int (*detach_adapter)(struct i2c_adapter *);

	/* tells the driver that a client is about to be deleted & gives it 
	 * the chance to remove its private data. Also, if the client struct
	 * has been dynamically allocated by the driver in the function above,
	 * it must be freed here.
	 */
	int (*detach_client)(struct i2c_client *);
	
	/* a ioctl like command that can be used to perform specific functions
	 * with the device.
	 */
	int (*command)(struct i2c_client *client,unsigned int cmd, void *arg);

	struct device_driver driver;
	struct list_head list;
};
#define to_i2c_driver(d) container_of(d, struct i2c_driver, driver)

#define I2C_NAME_SIZE	50

/*
 * i2c_client identifies a single device (i.e. chip) that is connected to an 
 * i2c bus. The behaviour is defined by the routines of the driver. This
 * function is mainly used for lookup & other admin. functions.
 */
struct i2c_client {
	unsigned int flags;		/* div., see below		*/
	unsigned short addr;		/* chip address - NOTE: 7bit 	*/
					/* addresses are stored in the	*/
					/* _LOWER_ 7 bits		*/
	struct i2c_adapter *adapter;	/* the adapter we sit on	*/
	struct i2c_driver *driver;	/* and our access routines	*/
	int usage_count;		/* How many accesses currently  */
					/* to the client		*/
	struct device dev;		/* the device structure		*/
	struct list_head list;
	char name[I2C_NAME_SIZE];
	struct completion released;
};
#define to_i2c_client(d) container_of(d, struct i2c_client, dev)

static inline struct i2c_client *kobj_to_i2c_client(struct kobject *kobj)
{
	return to_i2c_client(container_of(kobj, struct device, kobj));
}

static inline void *i2c_get_clientdata (struct i2c_client *dev)
{
	return dev_get_drvdata (&dev->dev);
}

static inline void i2c_set_clientdata (struct i2c_client *dev, void *data)
{
	dev_set_drvdata (&dev->dev, data);
}

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
	int (*master_xfer)(struct i2c_adapter *adap,struct i2c_msg *msgs, 
	                   int num);
	int (*smbus_xfer) (struct i2c_adapter *adap, u16 addr, 
	                   unsigned short flags, char read_write,
	                   u8 command, int size, union i2c_smbus_data * data);

	/* --- these optional/future use for some adapter types.*/
	int (*slave_send)(struct i2c_adapter *,char*,int);
	int (*slave_recv)(struct i2c_adapter *,char*,int);

	/* --- ioctl like call to set div. parameters. */
	int (*algo_control)(struct i2c_adapter *, unsigned int, unsigned long);

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
	struct i2c_algorithm *algo;/* the algorithm to access the bus	*/
	void *algo_data;

	/* --- administration stuff. */
	int (*client_register)(struct i2c_client *);
	int (*client_unregister)(struct i2c_client *);

	/* data fields that are valid for all devices	*/
	struct mutex bus_lock;
	struct mutex clist_lock;

	int timeout;
	int retries;
	struct device dev;		/* the adapter device */
	struct class_device class_dev;	/* the class device */

	int nr;
	struct list_head clients;
	struct list_head list;
	char name[I2C_NAME_SIZE];
	struct completion dev_released;
	struct completion class_dev_released;
};
#define dev_to_i2c_adapter(d) container_of(d, struct i2c_adapter, dev)
#define class_dev_to_i2c_adapter(d) container_of(d, struct i2c_adapter, class_dev)

static inline void *i2c_get_adapdata (struct i2c_adapter *dev)
{
	return dev_get_drvdata (&dev->dev);
}

static inline void i2c_set_adapdata (struct i2c_adapter *dev, void *data)
{
	dev_set_drvdata (&dev->dev, data);
}

/*flags for the client struct: */
#define I2C_CLIENT_PEC  0x04			/* Use Packet Error Checking */
#define I2C_CLIENT_TEN	0x10			/* we have a ten bit chip address	*/
						/* Must equal I2C_M_TEN below */

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
#define ANY_I2C_ISA_BUS		9191


/* ----- functions exported by i2c.o */

/* administration...
 */
extern int i2c_add_adapter(struct i2c_adapter *);
extern int i2c_del_adapter(struct i2c_adapter *);

extern int i2c_register_driver(struct module *, struct i2c_driver *);
extern int i2c_del_driver(struct i2c_driver *);

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

/* An ioctl like call to set div. parameters of the adapter.
 */
extern int i2c_control(struct i2c_client *,unsigned int, unsigned long);

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

/*
 * I2C Message - used for pure i2c transaction, also from /dev interface
 */
struct i2c_msg {
	__u16 addr;	/* slave address			*/
 	__u16 flags;		
#define I2C_M_TEN	0x10	/* we have a ten bit chip address	*/
#define I2C_M_RD	0x01
#define I2C_M_NOSTART	0x4000
#define I2C_M_REV_DIR_ADDR	0x2000
#define I2C_M_IGNORE_NAK	0x1000
#define I2C_M_NO_RD_ACK		0x0800
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
#define I2C_SMBUS_I2C_BLOCK_DATA    6
#define I2C_SMBUS_BLOCK_PROC_CALL   7		/* SMBus 2.0 */


/* ----- commands for the ioctl like i2c_command call:
 * note that additional calls are defined in the algorithm and hw 
 *	dependent layers - these can be listed here, or see the 
 *	corresponding header files.
 */
				/* -> bit-adapter specific ioctls	*/
#define I2C_RETRIES	0x0701	/* number of times a device address      */
				/* should be polled when not            */
                                /* acknowledging 			*/
#define I2C_TIMEOUT	0x0702	/* set timeout - call with int 		*/


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
