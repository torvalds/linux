/*
 * dvbdev.h
 *
 * Copyright (C) 2000 Ralph Metzler & Marcus Metzler
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DVBDEV_H_
#define _DVBDEV_H_

#include <linux/types.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <media/media-device.h>

#define DVB_MAJOR 212

#if defined(CONFIG_DVB_MAX_ADAPTERS) && CONFIG_DVB_MAX_ADAPTERS > 0
  #define DVB_MAX_ADAPTERS CONFIG_DVB_MAX_ADAPTERS
#else
  #define DVB_MAX_ADAPTERS 16
#endif

#define DVB_UNSET (-1)

/* List of DVB device types */

/**
 * enum dvb_device_type - type of the Digital TV device
 *
 * @DVB_DEVICE_SEC:		Digital TV standalone Common Interface (CI)
 * @DVB_DEVICE_FRONTEND:	Digital TV frontend.
 * @DVB_DEVICE_DEMUX:		Digital TV demux.
 * @DVB_DEVICE_DVR:		Digital TV digital video record (DVR).
 * @DVB_DEVICE_CA:		Digital TV Conditional Access (CA).
 * @DVB_DEVICE_NET:		Digital TV network.
 *
 * @DVB_DEVICE_VIDEO:		Digital TV video decoder.
 *				Deprecated. Used only on av7110-av.
 * @DVB_DEVICE_AUDIO:		Digital TV audio decoder.
 *				Deprecated. Used only on av7110-av.
 * @DVB_DEVICE_OSD:		Digital TV On Screen Display (OSD).
 *				Deprecated. Used only on av7110.
 */
enum dvb_device_type {
	DVB_DEVICE_SEC,
	DVB_DEVICE_FRONTEND,
	DVB_DEVICE_DEMUX,
	DVB_DEVICE_DVR,
	DVB_DEVICE_CA,
	DVB_DEVICE_NET,

	DVB_DEVICE_VIDEO,
	DVB_DEVICE_AUDIO,
	DVB_DEVICE_OSD,
};

#define DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr) \
	static short adapter_nr[] = \
		{[0 ... (DVB_MAX_ADAPTERS - 1)] = DVB_UNSET }; \
	module_param_array(adapter_nr, short, NULL, 0444); \
	MODULE_PARM_DESC(adapter_nr, "DVB adapter numbers")

struct dvb_frontend;

/**
 * struct dvb_adapter - represents a Digital TV adapter using Linux DVB API
 *
 * @num:		Number of the adapter
 * @list_head:		List with the DVB adapters
 * @device_list:	List with the DVB devices
 * @name:		Name of the adapter
 * @proposed_mac:	proposed MAC address for the adapter
 * @priv:		private data
 * @device:		pointer to struct device
 * @module:		pointer to struct module
 * @mfe_shared:		indicates mutually exclusive frontends.
 *			Use of this flag is currently deprecated.
 * @mfe_dvbdev:		Frontend device in use, in the case of MFE
 * @mfe_lock:		Lock to prevent using the other frontends when MFE is
 *			used.
 * @mdev_lock:          Protect access to the mdev pointer.
 * @mdev:		pointer to struct media_device, used when the media
 *			controller is used.
 * @conn:		RF connector. Used only if the device has no separate
 *			tuner.
 * @conn_pads:		pointer to struct media_pad associated with @conn;
 */
struct dvb_adapter {
	int num;
	struct list_head list_head;
	struct list_head device_list;
	const char *name;
	u8 proposed_mac [6];
	void* priv;

	struct device *device;

	struct module *module;

	int mfe_shared;			/* indicates mutually exclusive frontends */
	struct dvb_device *mfe_dvbdev;	/* frontend device in use */
	struct mutex mfe_lock;		/* access lock for thread creation */

#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	struct mutex mdev_lock;
	struct media_device *mdev;
	struct media_entity *conn;
	struct media_pad *conn_pads;
#endif
};

/**
 * struct dvb_device - represents a DVB device node
 *
 * @list_head:	List head with all DVB devices
 * @fops:	pointer to struct file_operations
 * @adapter:	pointer to the adapter that holds this device node
 * @type:	type of the device, as defined by &enum dvb_device_type.
 * @minor:	devnode minor number. Major number is always DVB_MAJOR.
 * @id:		device ID number, inside the adapter
 * @readers:	Initialized by the caller. Each call to open() in Read Only mode
 *		decreases this counter by one.
 * @writers:	Initialized by the caller. Each call to open() in Read/Write
 *		mode decreases this counter by one.
 * @users:	Initialized by the caller. Each call to open() in any mode
 *		decreases this counter by one.
 * @wait_queue:	wait queue, used to wait for certain events inside one of
 *		the DVB API callers
 * @kernel_ioctl: callback function used to handle ioctl calls from userspace.
 * @name:	Name to be used for the device at the Media Controller
 * @entity:	pointer to struct media_entity associated with the device node
 * @pads:	pointer to struct media_pad associated with @entity;
 * @priv:	private data
 * @intf_devnode: Pointer to media_intf_devnode. Used by the dvbdev core to
 *		store the MC device node interface
 * @tsout_num_entities: Number of Transport Stream output entities
 * @tsout_entity: array with MC entities associated to each TS output node
 * @tsout_pads: array with the source pads for each @tsout_entity
 *
 * This structure is used by the DVB core (frontend, CA, net, demux) in
 * order to create the device nodes. Usually, driver should not initialize
 * this struct diretly.
 */
struct dvb_device {
	struct list_head list_head;
	const struct file_operations *fops;
	struct dvb_adapter *adapter;
	enum dvb_device_type type;
	int minor;
	u32 id;

	/* in theory, 'users' can vanish now,
	   but I don't want to change too much now... */
	int readers;
	int writers;
	int users;

	wait_queue_head_t	  wait_queue;
	/* don't really need those !? -- FIXME: use video_usercopy  */
	int (*kernel_ioctl)(struct file *file, unsigned int cmd, void *arg);

	/* Needed for media controller register/unregister */
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	const char *name;

	/* Allocated and filled inside dvbdev.c */
	struct media_intf_devnode *intf_devnode;

	unsigned tsout_num_entities;
	struct media_entity *entity, *tsout_entity;
	struct media_pad *pads, *tsout_pads;
#endif

	void *priv;
};

/**
 * dvb_register_adapter - Registers a new DVB adapter
 *
 * @adap:	pointer to struct dvb_adapter
 * @name:	Adapter's name
 * @module:	initialized with THIS_MODULE at the caller
 * @device:	pointer to struct device that corresponds to the device driver
 * @adapter_nums: Array with a list of the numbers for @dvb_register_adapter;
 *		to select among them. Typically, initialized with:
 *		DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nums)
 */
int dvb_register_adapter(struct dvb_adapter *adap, const char *name,
			 struct module *module, struct device *device,
			 short *adapter_nums);

/**
 * dvb_unregister_adapter - Unregisters a DVB adapter
 *
 * @adap:	pointer to struct dvb_adapter
 */
int dvb_unregister_adapter(struct dvb_adapter *adap);

/**
 * dvb_register_device - Registers a new DVB device
 *
 * @adap:	pointer to struct dvb_adapter
 * @pdvbdev:	pointer to the place where the new struct dvb_device will be
 *		stored
 * @template:	Template used to create &pdvbdev;
 * @priv:	private data
 * @type:	type of the device, as defined by &enum dvb_device_type.
 * @demux_sink_pads: Number of demux outputs, to be used to create the TS
 *		outputs via the Media Controller.
 */
int dvb_register_device(struct dvb_adapter *adap,
			struct dvb_device **pdvbdev,
			const struct dvb_device *template,
			void *priv,
			enum dvb_device_type type,
			int demux_sink_pads);

/**
 * dvb_remove_device - Remove a registered DVB device
 *
 * This does not free memory.  To do that, call dvb_free_device().
 *
 * @dvbdev:	pointer to struct dvb_device
 */
void dvb_remove_device(struct dvb_device *dvbdev);

/**
 * dvb_free_device - Free memory occupied by a DVB device.
 *
 * Call dvb_unregister_device() before calling this function.
 *
 * @dvbdev:	pointer to struct dvb_device
 */
void dvb_free_device(struct dvb_device *dvbdev);

/**
 * dvb_unregister_device - Unregisters a DVB device
 *
 * This is a combination of dvb_remove_device() and dvb_free_device().
 * Using this function is usually a mistake, and is often an indicator
 * for a use-after-free bug (when a userspace process keeps a file
 * handle to a detached device).
 *
 * @dvbdev:	pointer to struct dvb_device
 */
void dvb_unregister_device(struct dvb_device *dvbdev);

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
/**
 * dvb_create_media_graph - Creates media graph for the Digital TV part of the
 *				device.
 *
 * @adap:			pointer to &struct dvb_adapter
 * @create_rf_connector:	if true, it creates the RF connector too
 *
 * This function checks all DVB-related functions at the media controller
 * entities and creates the needed links for the media graph. It is
 * capable of working with multiple tuners or multiple frontends, but it
 * won't create links if the device has multiple tuners and multiple frontends
 * or if the device has multiple muxes. In such case, the caller driver should
 * manually create the remaining links.
 */
__must_check int dvb_create_media_graph(struct dvb_adapter *adap,
					bool create_rf_connector);

/**
 * dvb_register_media_controller - registers a media controller at DVB adapter
 *
 * @adap:			pointer to &struct dvb_adapter
 * @mdev:			pointer to &struct media_device
 */
static inline void dvb_register_media_controller(struct dvb_adapter *adap,
						 struct media_device *mdev)
{
	adap->mdev = mdev;
}

/**
 * dvb_get_media_controller - gets the associated media controller
 *
 * @adap:			pointer to &struct dvb_adapter
 */
static inline struct media_device
*dvb_get_media_controller(struct dvb_adapter *adap)
{
	return adap->mdev;
}
#else
static inline
int dvb_create_media_graph(struct dvb_adapter *adap,
			   bool create_rf_connector)
{
	return 0;
};
#define dvb_register_media_controller(a, b) {}
#define dvb_get_media_controller(a) NULL
#endif

/**
 * dvb_generic_open - Digital TV open function, used by DVB devices
 *
 * @inode: pointer to &struct inode.
 * @file: pointer to &struct file.
 *
 * Checks if a DVB devnode is still valid, and if the permissions are
 * OK and increment negative use count.
 */
int dvb_generic_open(struct inode *inode, struct file *file);

/**
 * dvb_generic_close - Digital TV close function, used by DVB devices
 *
 * @inode: pointer to &struct inode.
 * @file: pointer to &struct file.
 *
 * Checks if a DVB devnode is still valid, and if the permissions are
 * OK and decrement negative use count.
 */
int dvb_generic_release(struct inode *inode, struct file *file);

/**
 * dvb_generic_ioctl - Digital TV close function, used by DVB devices
 *
 * @file: pointer to &struct file.
 * @cmd: Ioctl name.
 * @arg: Ioctl argument.
 *
 * Checks if a DVB devnode and struct dvbdev.kernel_ioctl is still valid.
 * If so, calls dvb_usercopy().
 */
long dvb_generic_ioctl(struct file *file,
		       unsigned int cmd, unsigned long arg);

/**
 * dvb_usercopy - copies data from/to userspace memory when an ioctl is
 *      issued.
 *
 * @file: Pointer to struct &file.
 * @cmd: Ioctl name.
 * @arg: Ioctl argument.
 * @func: function that will actually handle the ioctl
 *
 * Ancillary function that uses ioctl direction and size to copy from
 * userspace. Then, it calls @func, and, if needed, data is copied back
 * to userspace.
 */
int dvb_usercopy(struct file *file, unsigned int cmd, unsigned long arg,
		 int (*func)(struct file *file, unsigned int cmd, void *arg));

#if IS_ENABLED(CONFIG_I2C)

struct i2c_adapter;
struct i2c_client;
/**
 * dvb_module_probe - helper routine to probe an I2C module
 *
 * @module_name:
 *	Name of the I2C module to be probed
 * @name:
 *	Optional name for the I2C module. Used for debug purposes.
 * 	If %NULL, defaults to @module_name.
 * @adap:
 *	pointer to &struct i2c_adapter that describes the I2C adapter where
 *	the module will be bound.
 * @addr:
 *	I2C address of the adapter, in 7-bit notation.
 * @platform_data:
 *	Platform data to be passed to the I2C module probed.
 *
 * This function binds an I2C device into the DVB core. Should be used by
 * all drivers that use I2C bus to control the hardware. A module bound
 * with dvb_module_probe() should use dvb_module_release() to unbind.
 *
 * Return:
 *	On success, return an &struct i2c_client, pointing the the bound
 *	I2C device. %NULL otherwise.
 *
 * .. note::
 *
 *    In the past, DVB modules (mainly, frontends) were bound via dvb_attach()
 *    macro, with does an ugly hack, using I2C low level functions. Such
 *    usage is deprecated and will be removed soon. Instead, use this routine.
 */
struct i2c_client *dvb_module_probe(const char *module_name,
				    const char *name,
				    struct i2c_adapter *adap,
				    unsigned char addr,
				    void *platform_data);

/**
 * dvb_module_release - releases an I2C device allocated with
 *	 dvb_module_probe().
 *
 * @client: pointer to &struct i2c_client with the I2C client to be released.
 *	    can be %NULL.
 *
 * This function should be used to free all resources reserved by
 * dvb_module_probe() and unbinding the I2C hardware.
 */
void dvb_module_release(struct i2c_client *client);

#endif /* CONFIG_I2C */

/* Legacy generic DVB attach function. */
#ifdef CONFIG_MEDIA_ATTACH

/**
 * dvb_attach - attaches a DVB frontend into the DVB core.
 *
 * @FUNCTION:	function on a frontend module to be called.
 * @ARGS...:	@FUNCTION arguments.
 *
 * This ancillary function loads a frontend module in runtime and runs
 * the @FUNCTION function there, with @ARGS.
 * As it increments symbol usage cont, at unregister, dvb_detach()
 * should be called.
 *
 * .. note::
 *
 *    In the past, DVB modules (mainly, frontends) were bound via dvb_attach()
 *    macro, with does an ugly hack, using I2C low level functions. Such
 *    usage is deprecated and will be removed soon. Instead, you should use
 *    dvb_module_probe().
 */
#define dvb_attach(FUNCTION, ARGS...) ({ \
	void *__r = NULL; \
	typeof(&FUNCTION) __a = symbol_request(FUNCTION); \
	if (__a) { \
		__r = (void *) __a(ARGS); \
		if (__r == NULL) \
			symbol_put(FUNCTION); \
	} else { \
		printk(KERN_ERR "DVB: Unable to find symbol "#FUNCTION"()\n"); \
	} \
	__r; \
})

/**
 * dvb_detach - detaches a DVB frontend loaded via dvb_attach()
 *
 * @FUNC:	attach function
 *
 * Decrements usage count for a function previously called via dvb_attach().
 */

#define dvb_detach(FUNC)	symbol_put_addr(FUNC)

#else
#define dvb_attach(FUNCTION, ARGS...) ({ \
	FUNCTION(ARGS); \
})

#define dvb_detach(FUNC)	{}

#endif	/* CONFIG_MEDIA_ATTACH */

#endif /* #ifndef _DVBDEV_H_ */
