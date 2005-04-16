/*
 * sound/maui.c
 *
 * The low level driver for Turtle Beach Maui and Tropez.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *	Changes:
 *		Alan Cox		General clean up, use kernel IRQ 
 *					system
 *		Christoph Hellwig	Adapted to module_init/module_exit
 *		Bartlomiej Zolnierkiewicz
 *					Added __init to download_code()
 *
 *	Status:
 *		Andrew J. Kroll		Tested 06/01/1999 with:
 *					* OSWF.MOT File Version: 1.15
 *					* OSWF.MOT File Dated: 09/12/94
 *					* Older versions will cause problems.
 */

#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#define USE_SEQ_MACROS
#define USE_SIMPLE_MACROS

#include "sound_config.h"
#include "sound_firmware.h"

#include "mpu401.h"

static int      maui_base = 0x330;

static volatile int irq_ok;
static int     *maui_osp;

#define HOST_DATA_PORT	(maui_base + 2)
#define HOST_STAT_PORT	(maui_base + 3)
#define HOST_CTRL_PORT	(maui_base + 3)

#define STAT_TX_INTR	0x40
#define STAT_TX_AVAIL	0x20
#define STAT_TX_IENA	0x10
#define STAT_RX_INTR	0x04
#define STAT_RX_AVAIL	0x02
#define STAT_RX_IENA	0x01

static int      (*orig_load_patch)(int dev, int format, const char __user *addr,
			      int offs, int count, int pmgr_flag) = NULL;

#include "maui_boot.h"

static int maui_wait(int mask)
{
	int i;

	/*
	 * Perform a short initial wait without sleeping
	 */

	for (i = 0; i < 100; i++)
		if (inb(HOST_STAT_PORT) & mask)
			return 1;

	/*
	 * Wait up to 15 seconds with sleeping
	 */

	for (i = 0; i < 150; i++) {
		if (inb(HOST_STAT_PORT) & mask)
			return 1;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ / 10);
		if (signal_pending(current))
			return 0;
	}
	return 0;
}

static int maui_read(void)
{
	if (maui_wait(STAT_RX_AVAIL))
		return inb(HOST_DATA_PORT);
	return -1;
}

static int maui_write(unsigned char data)
{
	if (maui_wait(STAT_TX_AVAIL)) {
		outb((data), HOST_DATA_PORT);
		return 1;
	}
	printk(KERN_WARNING "Maui: Write timeout\n");
	return 0;
}

static irqreturn_t mauiintr(int irq, void *dev_id, struct pt_regs *dummy)
{
	irq_ok = 1;
	return IRQ_HANDLED;
}

static int __init download_code(void)
{
	int i, lines = 0;
	int eol_seen = 0, done = 0;
	int skip = 1;

	printk(KERN_INFO "Code download (%d bytes): ", maui_osLen);

	for (i = 0; i < maui_osLen; i++) {
		if (maui_os[i] != '\r') {
			if (!skip || (maui_os[i] == 'S' && (i == 0 || maui_os[i - 1] == '\n'))) {
				skip = 0;

				if (maui_os[i] == '\n')
					eol_seen = skip = 1;
				else if (maui_os[i] == 'S') {
					if (maui_os[i + 1] == '8')
						done = 1;
					if (!maui_write(0xF1))
						goto failure;
					if (!maui_write('S'))
						goto failure;
				} else {
					if (!maui_write(maui_os[i]))
						goto failure;
				}

				if (eol_seen) {
					int c = 0;
					int n;

					eol_seen = 0;

					for (n = 0; n < 2; n++) {
						if (maui_wait(STAT_RX_AVAIL)) {
							c = inb(HOST_DATA_PORT);
							break;
						}
					}
					if (c != 0x80) {
						printk("Download not acknowledged\n");
						return 0;
					}
					else if (!(lines++ % 10))
						printk(".");

					if (done) {
						printk("\n");
						printk(KERN_INFO "Download complete\n");
						return 1;
					}
				}
			}
		}
	}

failure:
	printk("\n");
	printk(KERN_ERR "Download failed!!!\n");
	return 0;
}

static int __init maui_init(int irq)
{
	unsigned char bits;

	switch (irq) {
		case 9:
			bits = 0x00;
			break;
		case 5:
			bits = 0x08;
			break;
		case 12:
			bits = 0x10;
			break;
		case 15:
			bits = 0x18;
			break;

		default:
			printk(KERN_ERR "Maui: Invalid IRQ %d\n", irq);
			return 0;
	}
	outb((0x00), HOST_CTRL_PORT);	/* Reset */
	outb((bits), HOST_DATA_PORT);	/* Set the IRQ bits */
	outb((bits | 0x80), HOST_DATA_PORT);	/* Set the IRQ bits again? */
	outb((0x80), HOST_CTRL_PORT);	/* Leave reset */
	outb((0x80), HOST_CTRL_PORT);	/* Leave reset */
	outb((0xD0), HOST_CTRL_PORT);	/* Cause interrupt */

#ifdef CONFIG_SMP
	{
		int i;
		for (i = 0; i < 1000000 && !irq_ok; i++)
			;
		if (!irq_ok)
			return 0;
	}
#endif
	outb((0x80), HOST_CTRL_PORT);	/* Leave reset */

	printk(KERN_INFO "Turtle Beach Maui initialization\n");

	if (!download_code())
		return 0;

	outb((0xE0), HOST_CTRL_PORT);	/* Normal operation */

	/* Select mpu401 mode */

	maui_write(0xf0);
	maui_write(1);
	if (maui_read() != 0x80) {
		maui_write(0xf0);
		maui_write(1);
		if (maui_read() != 0x80)
			printk(KERN_ERR "Maui didn't acknowledge set HW mode command\n");
	}
	printk(KERN_INFO "Maui initialized OK\n");
	return 1;
}

static int maui_short_wait(int mask) {
	int i;

	for (i = 0; i < 1000; i++) {
		if (inb(HOST_STAT_PORT) & mask) {
			return 1;
		}
	}
	return 0;
}

static int maui_load_patch(int dev, int format, const char __user *addr,
		int offs, int count, int pmgr_flag)
{

	struct sysex_info header;
	unsigned long left, src_offs;
	int hdr_size = (unsigned long) &header.data[0] - (unsigned long) &header;
	int i;

	if (format == SYSEX_PATCH)	/* Handled by midi_synth.c */
		return orig_load_patch(dev, format, addr, offs, count, pmgr_flag);

	if (format != MAUI_PATCH)
	{
		  printk(KERN_WARNING "Maui: Unknown patch format\n");
	}
	if (count < hdr_size) {
/*		  printk("Maui error: Patch header too short\n");*/
		  return -EINVAL;
	}
	count -= hdr_size;

	/*
	 * Copy the header from user space but ignore the first bytes which have
	 * been transferred already.
	 */

	if(copy_from_user(&((char *) &header)[offs], &(addr)[offs], hdr_size - offs))
		return -EFAULT;

	if (count < header.len) {
		  printk(KERN_ERR "Maui warning: Host command record too short (%d<%d)\n", count, (int) header.len);
		  header.len = count;
	}
	left = header.len;
	src_offs = 0;

	for (i = 0; i < left; i++) {
		unsigned char   data;

		if(get_user(*(unsigned char *) &data, (unsigned char __user *) &((addr)[hdr_size + i])))
			return -EFAULT;
		if (i == 0 && !(data & 0x80))
			return -EINVAL;

		if (maui_write(data) == -1)
			return -EIO;
	}

	if ((i = maui_read()) != 0x80) {
		if (i != -1)
			printk("Maui: Error status %02x\n", i);
		return -EIO;
	}
	return 0;
}

static int __init probe_maui(struct address_info *hw_config)
{
	struct resource *ports;
	int this_dev;
	int i;
	int tmp1, tmp2, ret;

	ports = request_region(hw_config->io_base, 2, "mpu401");
	if (!ports)
		return 0;

	if (!request_region(hw_config->io_base + 2, 6, "Maui"))
		goto out;

	maui_base = hw_config->io_base;
	maui_osp = hw_config->osp;

	if (request_irq(hw_config->irq, mauiintr, 0, "Maui", NULL) < 0)
		goto out2;

	/*
	 * Initialize the processor if necessary
	 */

	if (maui_osLen > 0) {
		if (!(inb(HOST_STAT_PORT) & STAT_TX_AVAIL) ||
			!maui_write(0x9F) ||	/* Report firmware version */
			!maui_short_wait(STAT_RX_AVAIL) ||
			maui_read() == -1 || maui_read() == -1)
			if (!maui_init(hw_config->irq))
				goto out3;
	}
	if (!maui_write(0xCF))	/* Report hardware version */ {
		printk(KERN_ERR "No WaveFront firmware detected (card uninitialized?)\n");
		goto out3;
	}
	if ((tmp1 = maui_read()) == -1 || (tmp2 = maui_read()) == -1) {
		printk(KERN_ERR "No WaveFront firmware detected (card uninitialized?)\n");
		goto out3;
	}
	if (tmp1 == 0xff || tmp2 == 0xff)
		goto out3;
	printk(KERN_DEBUG "WaveFront hardware version %d.%d\n", tmp1, tmp2);

	if (!maui_write(0x9F))	/* Report firmware version */
		goto out3;
	if ((tmp1 = maui_read()) == -1 || (tmp2 = maui_read()) == -1)
		goto out3;

	printk(KERN_DEBUG "WaveFront firmware version %d.%d\n", tmp1, tmp2);

	if (!maui_write(0x85))	/* Report free DRAM */
		goto out3;
	tmp1 = 0;
	for (i = 0; i < 4; i++) {
		tmp1 |= maui_read() << (7 * i);
	}
	printk(KERN_DEBUG "Available DRAM %dk\n", tmp1 / 1024);

	for (i = 0; i < 1000; i++)
		if (probe_mpu401(hw_config, ports))
			break;

	ret = probe_mpu401(hw_config, ports);
	if (!ret)
		goto out3;

	conf_printf("Maui", hw_config);

	hw_config->irq *= -1;
	hw_config->name = "Maui";
	attach_mpu401(hw_config, THIS_MODULE);

	if (hw_config->slots[1] != -1)	/* The MPU401 driver installed itself */ {
		struct synth_operations *synth;

		this_dev = hw_config->slots[1];

		/*
		 * Intercept patch loading calls so that they can be handled
		 * by the Maui driver.
		 */

		synth = midi_devs[this_dev]->converter;
		if (synth != NULL) {
			synth->id = "MAUI";
			orig_load_patch = synth->load_patch;
			synth->load_patch = &maui_load_patch;
		} else
			printk(KERN_ERR "Maui: Can't install patch loader\n");
	}
	return 1;

out3:
	free_irq(hw_config->irq, NULL);
out2:
	release_region(hw_config->io_base + 2, 6);
out:
	release_region(hw_config->io_base, 2);
	return 0;
}

static void __exit unload_maui(struct address_info *hw_config)
{
	int irq = hw_config->irq;
	release_region(hw_config->io_base + 2, 6);
	unload_mpu401(hw_config);

	if (irq < 0)
		irq = -irq;
	if (irq > 0)
		free_irq(irq, NULL);
}

static int fw_load;

static struct address_info cfg;

static int __initdata io = -1;
static int __initdata irq = -1;

module_param(io, int, 0);
module_param(irq, int, 0);

/*
 *	Install a Maui card. Needs mpu401 loaded already.
 */

static int __init init_maui(void)
{
	printk(KERN_INFO "Turtle beach Maui and Tropez driver, Copyright (C) by Hannu Savolainen 1993-1996\n");

	cfg.io_base = io;
	cfg.irq = irq;

	if (cfg.io_base == -1 || cfg.irq == -1) {
		printk(KERN_INFO "maui: irq and io must be set.\n");
		return -EINVAL;
	}

	if (maui_os == NULL) {
		fw_load = 1;
		maui_osLen = mod_firmware_load("/etc/sound/oswf.mot", (char **) &maui_os);
	}
	if (probe_maui(&cfg) == 0)
		return -ENODEV;

	return 0;
}

static void __exit cleanup_maui(void)
{
	if (fw_load && maui_os)
		vfree(maui_os);
	unload_maui(&cfg);
}

module_init(init_maui);
module_exit(cleanup_maui);

#ifndef MODULE
static int __init setup_maui(char *str)
{
        /* io, irq */
	int ints[3];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io = ints[1];
	irq = ints[2];

	return 1;
}

__setup("maui=", setup_maui);
#endif
MODULE_LICENSE("GPL");
