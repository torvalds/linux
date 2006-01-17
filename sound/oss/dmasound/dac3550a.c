/*
 * Driver for the i2c/i2s based DAC3550a sound chip used
 * on some Apple iBooks. Also known as "DACA".
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <asm/io.h>

#include "dmasound.h"

/* FYI: This code was derived from the tas3001c.c Texas/Tumbler mixer
 * control code, as well as info derived from the AppleDACAAudio driver
 * from Darwin CVS (main thing I derived being register numbers and 
 * values, as well as when to make the calls). */

#define I2C_DRIVERID_DACA (0xFDCB)

#define DACA_VERSION	"0.1"
#define DACA_DATE "20010930"

static int cur_left_vol;
static int cur_right_vol;
static struct i2c_client *daca_client;

static int daca_attach_adapter(struct i2c_adapter *adapter);
static int daca_detect_client(struct i2c_adapter *adapter, int address);
static int daca_detach_client(struct i2c_client *client);

struct i2c_driver daca_driver = {  
	.driver = {
		.name		= "DAC3550A driver  V " DACA_VERSION,
	},
	.id			= I2C_DRIVERID_DACA,
	.attach_adapter		= daca_attach_adapter,
	.detach_client		= daca_detach_client,
};

#define VOL_MAX ((1<<20) - 1)

void daca_get_volume(uint * left_vol, uint  *right_vol)
{
	*left_vol = cur_left_vol >> 5;
	*right_vol = cur_right_vol >> 5;
}

int daca_set_volume(uint left_vol, uint right_vol)
{
	unsigned short voldata;
  
	if (!daca_client)
		return -1;

	/* Derived from experience, not from any specific values */
	left_vol <<= 5;
	right_vol <<= 5;

	if (left_vol > VOL_MAX)
		left_vol = VOL_MAX;
	if (right_vol > VOL_MAX)
		right_vol = VOL_MAX;

	voldata = ((left_vol >> 14)  & 0x3f) << 8;
	voldata |= (right_vol >> 14)  & 0x3f;
  
	if (i2c_smbus_write_word_data(daca_client, 2, voldata) < 0) {
		printk("daca: failed to set volume \n");
		return -1; 
	}

	cur_left_vol = left_vol;
	cur_right_vol = right_vol;
  
	return 0;
}

int daca_leave_sleep(void)
{
	if (!daca_client)
		return -1;
  
	/* Do a short sleep, just to make sure I2C bus is awake and paying
	 * attention to us
	 */
	msleep(20);
	/* Write the sample rate reg the value it needs */
	i2c_smbus_write_byte_data(daca_client, 1, 8);
	daca_set_volume(cur_left_vol >> 5, cur_right_vol >> 5);
	/* Another short delay, just to make sure the other I2C bus writes
	 * have taken...
	 */
	msleep(20);
	/* Write the global config reg - invert right power amp,
	 * DAC on, use 5-volt mode */
	i2c_smbus_write_byte_data(daca_client, 3, 0x45);

	return 0;
}

int daca_enter_sleep(void)
{
	if (!daca_client)
		return -1;

	i2c_smbus_write_byte_data(daca_client, 1, 8);
	daca_set_volume(cur_left_vol >> 5, cur_right_vol >> 5);

	/* Write the global config reg - invert right power amp,
	 * DAC on, enter low-power mode, use 5-volt mode
	 */
	i2c_smbus_write_byte_data(daca_client, 3, 0x65);

	return 0;
}

static int daca_attach_adapter(struct i2c_adapter *adapter)
{
	if (!strncmp(adapter->name, "mac-io", 6))
		daca_detect_client(adapter, 0x4d);
	return 0;
}

static int daca_init_client(struct i2c_client * new_client)
{
	/* 
	 * Probe is not working with the current i2c-keywest
	 * driver. We try to use addr 0x4d on each adapters
	 * instead, by setting the format register.
	 * 
	 * FIXME: I'm sure that can be obtained from the
	 * device-tree. --BenH.
	 */
  
	/* Write the global config reg - invert right power amp,
	 * DAC on, use 5-volt mode
	 */
	if (i2c_smbus_write_byte_data(new_client, 3, 0x45))
		return -1;

	i2c_smbus_write_byte_data(new_client, 1, 8);
	daca_client = new_client;
	daca_set_volume(15000, 15000);

	return 0;
}

static int daca_detect_client(struct i2c_adapter *adapter, int address)
{
	const char *client_name = "DAC 3550A Digital Equalizer";
	struct i2c_client *new_client;
	int rc = -ENODEV;

	new_client = kmalloc(sizeof(*new_client), GFP_KERNEL);
	if (!new_client)
		return -ENOMEM;
	memset(new_client, 0, sizeof(*new_client));

	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &daca_driver;
	new_client->flags = 0;
	strcpy(new_client->name, client_name);

	if (daca_init_client(new_client))
		goto bail;

	/* Tell the i2c layer a new client has arrived */
	if (i2c_attach_client(new_client))
		goto bail;

	return 0;
 bail:
	kfree(new_client);
	return rc;
}


static int daca_detach_client(struct i2c_client *client)
{
	if (client == daca_client)
		daca_client = NULL;

  	i2c_detach_client(client);
	kfree(client);
	return 0;
}

void daca_cleanup(void)
{
	i2c_del_driver(&daca_driver);
}

int daca_init(void)
{
	printk("dac3550a driver version %s (%s)\n",DACA_VERSION,DACA_DATE);
	return i2c_add_driver(&daca_driver);
}
