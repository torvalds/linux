/*
 * sound/oss/trix.c
 *
 * Low level driver for the MediaTrix AudioTrix Pro
 * (MT-0002-PC Control Chip)
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes
 *	Alan Cox		Modularisation, cleanup.
 *	Christoph Hellwig	Adapted to module_init/module_exit
 *	Arnaldo C. de Melo	Got rid of attach_uart401
 */
 
#include <linux/init.h>
#include <linux/module.h>

#include "sound_config.h"
#include "sb.h"
#include "sound_firmware.h"

#include "ad1848.h"
#include "mpu401.h"

#include "trix_boot.h"

static int mpu;

static bool joystick;

static unsigned char trix_read(int addr)
{
	outb(((unsigned char) addr), 0x390);	/* MT-0002-PC ASIC address */
	return inb(0x391);	/* MT-0002-PC ASIC data */
}

static void trix_write(int addr, int data)
{
	outb(((unsigned char) addr), 0x390);	/* MT-0002-PC ASIC address */
	outb(((unsigned char) data), 0x391);	/* MT-0002-PC ASIC data */
}

static void download_boot(int base)
{
	int i = 0, n = trix_boot_len;

	if (trix_boot_len == 0)
		return;

	trix_write(0xf8, 0x00);	/* ??????? */
	outb((0x01), base + 6);	/* Clear the internal data pointer */
	outb((0x00), base + 6);	/* Restart */

	/*
	   *  Write the boot code to the RAM upload/download register.
	   *  Each write increments the internal data pointer.
	 */
	outb((0x01), base + 6);	/* Clear the internal data pointer */
	outb((0x1A), 0x390);	/* Select RAM download/upload port */

	for (i = 0; i < n; i++)
		outb((trix_boot[i]), 0x391);
	for (i = n; i < 10016; i++)	/* Clear up to first 16 bytes of data RAM */
		outb((0x00), 0x391);
	outb((0x00), base + 6);	/* Reset */
	outb((0x50), 0x390);	/* ?????? */

}

static int trix_set_wss_port(struct address_info *hw_config)
{
	unsigned char   addr_bits;

	if (trix_read(0x15) != 0x71)	/* No ASIC signature */
	{
		MDB(printk(KERN_ERR "No AudioTrix ASIC signature found\n"));
		return 0;
	}

	/*
	 * Reset some registers.
	 */

	trix_write(0x13, 0);
	trix_write(0x14, 0);

	/*
	 * Configure the ASIC to place the codec to the proper I/O location
	 */

	switch (hw_config->io_base)
	{
		case 0x530:
			addr_bits = 0;
			break;
		case 0x604:
			addr_bits = 1;
			break;
		case 0xE80:
			addr_bits = 2;
			break;
		case 0xF40:
			addr_bits = 3;
			break;
		default:
			return 0;
	}

	trix_write(0x19, (trix_read(0x19) & 0x03) | addr_bits);
	return 1;
}

/*
 *    Probe and attach routines for the Windows Sound System mode of
 *      AudioTrix Pro
 */

static int __init init_trix_wss(struct address_info *hw_config)
{
	static unsigned char dma_bits[4] = {
		1, 2, 0, 3
	};
	struct resource *ports;
	int config_port = hw_config->io_base + 0;
	int dma1 = hw_config->dma, dma2 = hw_config->dma2;
	int old_num_mixers = num_mixers;
	u8 config, bits;
	int ret;
 
	switch(hw_config->irq) {
	case 7:
		bits = 8;
		break;
	case 9:
		bits = 0x10;
		break;
	case 10:
		bits = 0x18;
		break;
	case 11:
		bits = 0x20;
		break;
	default:
		printk(KERN_ERR "AudioTrix: Bad WSS IRQ %d\n", hw_config->irq);
		return 0;
	}

	switch (dma1) {
	case 0:
	case 1:
	case 3:
		break;
	default:
		printk(KERN_ERR "AudioTrix: Bad WSS DMA %d\n", dma1);
		return 0;
	}

	switch (dma2) {
	case -1:
	case 0:
	case 1:
	case 3:
		break;
	default:
		printk(KERN_ERR "AudioTrix: Bad capture DMA %d\n", dma2);
		return 0;
	}

	/*
	 * Check if the IO port returns valid signature. The original MS Sound
	 * system returns 0x04 while some cards (AudioTrix Pro for example)
	 * return 0x00.
	 */
	ports = request_region(hw_config->io_base + 4, 4, "ad1848");
	if (!ports) {
		printk(KERN_ERR "AudioTrix: MSS I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}

	if (!request_region(hw_config->io_base, 4, "MSS config")) {
		printk(KERN_ERR "AudioTrix: MSS I/O port conflict (%x)\n", hw_config->io_base);
		release_region(hw_config->io_base + 4, 4);
		return 0;
	}

	if (!trix_set_wss_port(hw_config))
		goto fail;

	config = inb(hw_config->io_base + 3);

	if ((config & 0x3f) != 0x00)
	{
		MDB(printk(KERN_ERR "No MSS signature detected on port 0x%x\n", hw_config->io_base));
		goto fail;
	}

	/*
	 * Check that DMA0 is not in use with a 8 bit board.
	 */

	if (dma1 == 0 && config & 0x80)
	{
		printk(KERN_ERR "AudioTrix: Can't use DMA0 with a 8 bit card slot\n");
		goto fail;
	}
	if (hw_config->irq > 9 && config & 0x80)
	{
		printk(KERN_ERR "AudioTrix: Can't use IRQ%d with a 8 bit card slot\n", hw_config->irq);
		goto fail;
	}

	ret = ad1848_detect(ports, NULL, hw_config->osp);
	if (!ret)
		goto fail;

	if (joystick==1)
		trix_write(0x15, 0x80);

	/*
	 * Set the IRQ and DMA addresses.
	 */

	outb((bits | 0x40), config_port);

	if (dma2 == -1 || dma2 == dma1)
	{
		  bits |= dma_bits[dma1];
		  dma2 = dma1;
	}
	else
	{
		unsigned char tmp;

		tmp = trix_read(0x13) & ~30;
		trix_write(0x13, tmp | 0x80 | (dma1 << 4));

		tmp = trix_read(0x14) & ~30;
		trix_write(0x14, tmp | 0x80 | (dma2 << 4));
	}

	outb((bits), config_port);	/* Write IRQ+DMA setup */

	hw_config->slots[0] = ad1848_init("AudioTrix Pro", ports,
					  hw_config->irq,
					  dma1,
					  dma2,
					  0,
					  hw_config->osp,
					  THIS_MODULE);

	if (num_mixers > old_num_mixers)	/* Mixer got installed */
	{
		AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_LINE);	/* Line in */
		AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_CD);
		AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_SYNTH);		/* OPL4 */
		AD1848_REROUTE(SOUND_MIXER_SPEAKER, SOUND_MIXER_ALTPCM);	/* SB */
	}
	return 1;

fail:
	release_region(hw_config->io_base, 4);
	release_region(hw_config->io_base + 4, 4);
	return 0;
}

static int __init probe_trix_sb(struct address_info *hw_config)
{

	int tmp;
	unsigned char conf;
	extern int sb_be_quiet;
	int old_quiet;
	static signed char irq_translate[] = {
		-1, -1, -1, 0, 1, 2, -1, 3
	};

	if (trix_boot_len == 0)
		return 0;	/* No boot code -> no fun */

	if ((hw_config->io_base & 0xffffff8f) != 0x200)
		return 0;

	tmp = hw_config->irq;
	if (tmp > 7)
		return 0;
	if (irq_translate[tmp] == -1)
		return 0;

	tmp = hw_config->dma;
	if (tmp != 1 && tmp != 3)
		return 0;

	if (!request_region(hw_config->io_base, 16, "soundblaster")) {
		printk(KERN_ERR "AudioTrix: SB I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}

	conf = 0x84;		/* DMA and IRQ enable */
	conf |= hw_config->io_base & 0x70;	/* I/O address bits */
	conf |= irq_translate[hw_config->irq];
	if (hw_config->dma == 3)
		conf |= 0x08;
	trix_write(0x1b, conf);

	download_boot(hw_config->io_base);

	hw_config->name = "AudioTrix SB";
	if (!sb_dsp_detect(hw_config, 0, 0, NULL)) {
		release_region(hw_config->io_base, 16);
		return 0;
	}

	hw_config->driver_use_1 = SB_NO_MIDI | SB_NO_MIXER | SB_NO_RECORDING;

	/* Prevent false alarms */
	old_quiet = sb_be_quiet;
	sb_be_quiet = 1;

	sb_dsp_init(hw_config, THIS_MODULE);

	sb_be_quiet = old_quiet;
	return 1;
}

static int __init probe_trix_mpu(struct address_info *hw_config)
{
	unsigned char conf;
	static int irq_bits[] = {
		-1, -1, -1, 1, 2, 3, -1, 4, -1, 5
	};

	if (hw_config->irq > 9)
	{
		printk(KERN_ERR "AudioTrix: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	if (irq_bits[hw_config->irq] == -1)
	{
		printk(KERN_ERR "AudioTrix: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	switch (hw_config->io_base)
	{
		case 0x330:
			conf = 0x00;
			break;
		case 0x370:
			conf = 0x04;
			break;
		case 0x3b0:
			conf = 0x08;
			break;
		case 0x3f0:
			conf = 0x0c;
			break;
		default:
			return 0;	/* Invalid port */
	}

	conf |= irq_bits[hw_config->irq] << 4;
	trix_write(0x19, (trix_read(0x19) & 0x83) | conf);
	hw_config->name = "AudioTrix Pro";
	return probe_uart401(hw_config, THIS_MODULE);
}

static void __exit unload_trix_wss(struct address_info *hw_config)
{
	int dma2 = hw_config->dma2;

	if (dma2 == -1)
		dma2 = hw_config->dma;

	release_region(0x390, 2);
	release_region(hw_config->io_base, 4);

	ad1848_unload(hw_config->io_base + 4,
		      hw_config->irq,
		      hw_config->dma,
		      dma2,
		      0);
	sound_unload_audiodev(hw_config->slots[0]);
}

static inline void __exit unload_trix_mpu(struct address_info *hw_config)
{
	unload_uart401(hw_config);
}

static inline void __exit unload_trix_sb(struct address_info *hw_config)
{
	sb_dsp_unload(hw_config, mpu);
}

static struct address_info cfg;
static struct address_info cfg2;
static struct address_info cfg_mpu;

static int sb;
static int fw_load;

static int __initdata io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma2	= -1;	/* Set this for modules that need it */
static int __initdata sb_io	= -1;
static int __initdata sb_dma	= -1;
static int __initdata sb_irq	= -1;
static int __initdata mpu_io	= -1;
static int __initdata mpu_irq	= -1;

module_param_hw(io, int, ioport, 0);
module_param_hw(irq, int, irq, 0);
module_param_hw(dma, int, dma, 0);
module_param_hw(dma2, int, dma, 0);
module_param_hw(sb_io, int, ioport, 0);
module_param_hw(sb_dma, int, dma, 0);
module_param_hw(sb_irq, int, irq, 0);
module_param_hw(mpu_io, int, ioport, 0);
module_param_hw(mpu_irq, int, irq, 0);
module_param(joystick, bool, 0);

static int __init init_trix(void)
{
	printk(KERN_INFO "MediaTrix audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma2;

	cfg2.io_base = sb_io;
	cfg2.irq = sb_irq;
	cfg2.dma = sb_dma;

	cfg_mpu.io_base = mpu_io;
	cfg_mpu.irq = mpu_irq;

	if (cfg.io_base == -1 || cfg.dma == -1 || cfg.irq == -1) {
		printk(KERN_INFO "I/O, IRQ, DMA and type are mandatory\n");
		return -EINVAL;
	}

	if (cfg2.io_base != -1 && (cfg2.irq == -1 || cfg2.dma == -1)) {
		printk(KERN_INFO "CONFIG_SB_IRQ and CONFIG_SB_DMA must be specified if SB_IO is set.\n");
		return -EINVAL;
	}
	if (cfg_mpu.io_base != -1 && cfg_mpu.irq == -1) {
		printk(KERN_INFO "CONFIG_MPU_IRQ must be specified if MPU_IO is set.\n");
		return -EINVAL;
	}
	if (!trix_boot)
	{
		fw_load = 1;
		trix_boot_len = mod_firmware_load("/etc/sound/trxpro.bin",
						    (char **) &trix_boot);
	}

	if (!request_region(0x390, 2, "AudioTrix")) {
		printk(KERN_ERR "AudioTrix: Config port I/O conflict\n");
		return -ENODEV;
	}

	if (!init_trix_wss(&cfg)) {
		release_region(0x390, 2);
		return -ENODEV;
	}

	/*
	 *    We must attach in the right order to get the firmware
	 *      loaded up in time.
	 */

	if (cfg2.io_base != -1) {
		sb = probe_trix_sb(&cfg2);
	}
	
	if (cfg_mpu.io_base != -1)
		mpu = probe_trix_mpu(&cfg_mpu);

	return 0;
}

static void __exit cleanup_trix(void)
{
	if (fw_load)
		vfree(trix_boot);
	if (sb)
		unload_trix_sb(&cfg2);
	if (mpu)
		unload_trix_mpu(&cfg_mpu);
	unload_trix_wss(&cfg);
}

module_init(init_trix);
module_exit(cleanup_trix);

#ifndef MODULE
static int __init setup_trix (char *str)
{
	/* io, irq, dma, dma2, sb_io, sb_irq, sb_dma, mpu_io, mpu_irq */
	int ints[9];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);

	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma2	= ints[4];
	sb_io	= ints[5];
	sb_irq	= ints[6];
	sb_dma	= ints[6];
	mpu_io	= ints[7];
	mpu_irq	= ints[8];

	return 1;
}

__setup("trix=", setup_trix);
#endif
MODULE_LICENSE("GPL");
