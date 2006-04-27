#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/soundcard.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/prom.h>

#include "tas_common.h"

#define CALL0(proc)								\
	do {									\
		struct tas_data_t *self;					\
		if (!tas_client || driver_hooks == NULL)			\
			return -1;						\
		self = dev_get_drvdata(&tas_client->dev);			\
		if (driver_hooks->proc)						\
			return driver_hooks->proc(self);			\
		else								\
			return -EINVAL;						\
	} while (0)

#define CALL(proc,arg...)							\
	do {									\
		struct tas_data_t *self;					\
		if (!tas_client || driver_hooks == NULL)			\
			return -1;						\
		self = dev_get_drvdata(&tas_client->dev);			\
		if (driver_hooks->proc)						\
			return driver_hooks->proc(self, ## arg);		\
		else								\
			return -EINVAL;						\
	} while (0)


static u8 tas_i2c_address = 0x34;
static struct i2c_client *tas_client;
static struct device_node* tas_node;

static int tas_attach_adapter(struct i2c_adapter *);
static int tas_detach_client(struct i2c_client *);

struct i2c_driver tas_driver = {
	.driver = {
		.name	= "tas",
	},
	.attach_adapter	= tas_attach_adapter,
	.detach_client	= tas_detach_client,
};

struct tas_driver_hooks_t *driver_hooks;

int
tas_register_driver(struct tas_driver_hooks_t *hooks)
{
	driver_hooks = hooks;
	return 0;
}

int
tas_get_mixer_level(int mixer, uint *level)
{
	CALL(get_mixer_level,mixer,level);
}

int
tas_set_mixer_level(int mixer,uint level)
{
	CALL(set_mixer_level,mixer,level);
}

int
tas_enter_sleep(void)
{
	CALL0(enter_sleep);
}

int
tas_leave_sleep(void)
{
	CALL0(leave_sleep);
}

int
tas_supported_mixers(void)
{
	CALL0(supported_mixers);
}

int
tas_mixer_is_stereo(int mixer)
{
	CALL(mixer_is_stereo,mixer);
}

int
tas_stereo_mixers(void)
{
	CALL0(stereo_mixers);
}

int
tas_output_device_change(int device_id,int layout_id,int speaker_id)
{
	CALL(output_device_change,device_id,layout_id,speaker_id);
}

int
tas_device_ioctl(u_int cmd, u_long arg)
{
	CALL(device_ioctl,cmd,arg);
}

int
tas_post_init(void)
{
	CALL0(post_init);
}

static int
tas_detect_client(struct i2c_adapter *adapter, int address)
{
	static const char *client_name = "tas Digital Equalizer";
	struct i2c_client *new_client;
	int rc = -ENODEV;

	if (!driver_hooks) {
		printk(KERN_ERR "tas_detect_client called with no hooks !\n");
		return -ENODEV;
	}
	
	new_client = kmalloc(sizeof(*new_client), GFP_KERNEL);
	if (!new_client)
		return -ENOMEM;
	memset(new_client, 0, sizeof(*new_client));

	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &tas_driver;
	strlcpy(new_client->name, client_name, DEVICE_NAME_SIZE);

        if (driver_hooks->init(new_client))
		goto bail;

	/* Tell the i2c layer a new client has arrived */
	if (i2c_attach_client(new_client)) {
		driver_hooks->uninit(dev_get_drvdata(&new_client->dev));
		goto bail;
	}

	tas_client = new_client;
	return 0;
 bail:
	tas_client = NULL;
	kfree(new_client);
	return rc;
}

static int
tas_attach_adapter(struct i2c_adapter *adapter)
{
	if (!strncmp(adapter->name, "mac-io", 6))
		return tas_detect_client(adapter, tas_i2c_address);
	return 0;
}

static int
tas_detach_client(struct i2c_client *client)
{
	if (client == tas_client) {
		driver_hooks->uninit(dev_get_drvdata(&client->dev));

		i2c_detach_client(client);
		kfree(client);
	}
	return 0;
}

void
tas_cleanup(void)
{
	i2c_del_driver(&tas_driver);
}

int __init
tas_init(int driver_id, const char *driver_name)
{
	u32* paddr;

	printk(KERN_INFO "tas driver [%s])\n", driver_name);

#ifndef CONFIG_I2C_POWERMAC
	request_module("i2c-powermac");
#endif
	tas_node = find_devices("deq");
	if (tas_node == NULL)
		return -ENODEV;
	paddr = (u32 *)get_property(tas_node, "i2c-address", NULL);
	if (paddr) {
		tas_i2c_address = (*paddr) >> 1;
		printk(KERN_INFO "using i2c address: 0x%x from device-tree\n",
				tas_i2c_address);
	} else    
		printk(KERN_INFO "using i2c address: 0x%x (default)\n",
				tas_i2c_address);

	return i2c_add_driver(&tas_driver);
}
