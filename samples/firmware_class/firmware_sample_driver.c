/*
 * firmware_sample_driver.c -
 *
 * Copyright (c) 2003 Manuel Estrada Sainz
 *
 * Sample code on how to use request_firmware() from drivers.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/firmware.h>

static struct device ghost_device = {
	.bus_id    = "ghost0",
};


static void sample_firmware_load(char *firmware, int size)
{
	u8 buf[size+1];
	memcpy(buf, firmware, size);
	buf[size] = '\0';
	printk(KERN_INFO "firmware_sample_driver: firmware: %s\n", buf);
}

static void sample_probe_default(void)
{
	/* uses the default method to get the firmware */
	const struct firmware *fw_entry;
	int retval;

	printk(KERN_INFO "firmware_sample_driver: "
		"a ghost device got inserted :)\n");

	retval = request_firmware(&fw_entry, "sample_driver_fw", &ghost_device);
	if (retval) {
		printk(KERN_ERR
		       "firmware_sample_driver: Firmware not available\n");
		return;
	}

	sample_firmware_load(fw_entry->data, fw_entry->size);

	release_firmware(fw_entry);

	/* finish setting up the device */
}

static void sample_probe_specific(void)
{
	int retval;
	/* Uses some specific hotplug support to get the firmware from
	 * userspace  directly into the hardware, or via some sysfs file */

	/* NOTE: This currently doesn't work */

	printk(KERN_INFO "firmware_sample_driver: "
		"a ghost device got inserted :)\n");

	retval = request_firmware(NULL, "sample_driver_fw", &ghost_device);
	if (retval) {
		printk(KERN_ERR
		       "firmware_sample_driver: Firmware load failed\n");
		return;
	}

	/* request_firmware blocks until userspace finished, so at
	 * this point the firmware should be already in the device */

	/* finish setting up the device */
}

static void sample_probe_async_cont(const struct firmware *fw, void *context)
{
	if (!fw) {
		printk(KERN_ERR
		       "firmware_sample_driver: firmware load failed\n");
		return;
	}

	printk(KERN_INFO "firmware_sample_driver: device pointer \"%s\"\n",
	       (char *)context);
	sample_firmware_load(fw->data, fw->size);
}

static void sample_probe_async(void)
{
	/* Let's say that I can't sleep */
	int error;
	error = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG,
					"sample_driver_fw", &ghost_device,
					"my device pointer",
					sample_probe_async_cont);
	if (error)
		printk(KERN_ERR "firmware_sample_driver:"
		       " request_firmware_nowait failed\n");
}

static int sample_init(void)
{
	device_initialize(&ghost_device);
	/* since there is no real hardware insertion I just call the
	 * sample probe functions here */
	sample_probe_specific();
	sample_probe_default();
	sample_probe_async();
	return 0;
}

static void __exit sample_exit(void)
{
}

module_init(sample_init);
module_exit(sample_exit);

MODULE_LICENSE("GPL");
