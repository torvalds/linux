/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * mad16.c
 *
 * Initialization code for OPTi MAD16 compatible audio chips. Including
 *
 *      OPTi 82C928     MAD16           (replaced by C929)
 *      OAK OTI-601D    Mozart
 *      OAK OTI-605	Mozart		(later version with MPU401 Midi)
 *      OPTi 82C929     MAD16 Pro
 *      OPTi 82C930
 *      OPTi 82C924
 *
 * These audio interface chips don't produce sound themselves. They just
 * connect some other components (OPL-[234] and a WSS compatible codec)
 * to the PC bus and perform I/O, DMA and IRQ address decoding. There is
 * also a UART for the MPU-401 mode (not 82C928/Mozart).
 * The Mozart chip appears to be compatible with the 82C928, although later
 * issues of the card, using the OTI-605 chip, have an MPU-401 compatible Midi
 * port. This port is configured differently to that of the OPTi audio chips.
 *
 *	Changes
 *	
 *	Alan Cox		Clean up, added module selections.
 *
 *	A. Wik			Added support for Opti924 PnP.
 *				Improved debugging support.	16-May-1998
 *				Fixed bug.			16-Jun-1998
 *
 *      Torsten Duwe            Made Opti924 PnP support non-destructive
 *                                                             	23-Dec-1998
 *
 *	Paul Grayson		Added support for Midi on later Mozart cards.
 *								25-Nov-1999
 *	Christoph Hellwig	Adapted to module_init/module_exit.
 *	Arnaldo C. de Melo	got rid of attach_uart401       21-Sep-2000
 *
 *	Pavel Rabel		Clean up                           Nov-2000
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gameport.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include "ad1848.h"
#include "sb.h"
#include "mpu401.h"

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

static int      mad16_conf;
static int      mad16_cdsel;
static DEFINE_SPINLOCK(lock);

#define C928	1
#define MOZART	2
#define C929	3
#define C930	4
#define C924    5

/*
 *    Registers
 *
 *      The MAD16 occupies I/O ports 0xf8d to 0xf93 (fixed locations).
 *      All ports are inactive by default. They can be activated by
 *      writing 0xE2 or 0xE3 to the password register. The password is valid
 *      only until the next I/O read or write.
 *
 *      82C930 uses 0xE4 as the password and indirect addressing to access
 *      the config registers.
 */

#define MC0_PORT	0xf8c	/* Dummy port */
#define MC1_PORT	0xf8d	/* SB address, CD-ROM interface type, joystick */
#define MC2_PORT	0xf8e	/* CD-ROM address, IRQ, DMA, plus OPL4 bit */
#define MC3_PORT	0xf8f
#define PASSWD_REG	0xf8f
#define MC4_PORT	0xf90
#define MC5_PORT	0xf91
#define MC6_PORT	0xf92
#define MC7_PORT	0xf93
#define MC8_PORT	0xf94
#define MC9_PORT	0xf95
#define MC10_PORT	0xf96
#define MC11_PORT	0xf97
#define MC12_PORT	0xf98

static int      board_type = C928;

static int     *mad16_osp;
static int	c931_detected;	/* minor differences from C930 */
static char	c924pnp;	/* "     "           "    C924 */
static int	debug;  	/* debugging output */

#ifdef DDB
#undef DDB
#endif
#define DDB(x) do {if (debug) x;} while (0)

static unsigned char mad_read(int port)
{
	unsigned long flags;
	unsigned char tmp;

	spin_lock_irqsave(&lock,flags);

	switch (board_type)	/* Output password */
	{
		case C928:
		case MOZART:
			outb((0xE2), PASSWD_REG);
			break;

		case C929:
			outb((0xE3), PASSWD_REG);
			break;

		case C930:
			/* outb(( 0xE4),  PASSWD_REG); */
			break;

		case C924:
			/* the c924 has its ports relocated by -128 if
			   PnP is enabled  -aw */
			if (!c924pnp)
				outb((0xE5), PASSWD_REG); else
				outb((0xE5), PASSWD_REG - 0x80);
			break;
	}

	if (board_type == C930)
	{
		outb((port - MC0_PORT), 0xe0e);	/* Write to index reg */
		tmp = inb(0xe0f);	/* Read from data reg */
	}
	else
		if (!c924pnp)
			tmp = inb(port); else
			tmp = inb(port-0x80);
	spin_unlock_irqrestore(&lock,flags);

	return tmp;
}

static void mad_write(int port, int value)
{
	unsigned long   flags;

	spin_lock_irqsave(&lock,flags);

	switch (board_type)	/* Output password */
	{
		case C928:
		case MOZART:
			outb((0xE2), PASSWD_REG);
			break;

		case C929:
			outb((0xE3), PASSWD_REG);
			break;

		case C930:
			/* outb(( 0xE4),  PASSWD_REG); */
			break;

		case C924:
			if (!c924pnp)
				outb((0xE5), PASSWD_REG); else
				outb((0xE5), PASSWD_REG - 0x80);
			break;
	}

	if (board_type == C930)
	{
		outb((port - MC0_PORT), 0xe0e);	/* Write to index reg */
		outb(((unsigned char) (value & 0xff)), 0xe0f);
	}
	else
		if (!c924pnp)
			outb(((unsigned char) (value & 0xff)), port); else
			outb(((unsigned char) (value & 0xff)), port-0x80);
	spin_unlock_irqrestore(&lock,flags);
}

static int __init detect_c930(void)
{
	unsigned char   tmp = mad_read(MC1_PORT);

	if ((tmp & 0x06) != 0x06)
	{
		DDB(printk("Wrong C930 signature (%x)\n", tmp));
		/* return 0; */
	}
	mad_write(MC1_PORT, 0);

	if (mad_read(MC1_PORT) != 0x06)
	{
		DDB(printk("Wrong C930 signature2 (%x)\n", tmp));
		/* return 0; */
	}
	mad_write(MC1_PORT, tmp);	/* Restore bits */

	mad_write(MC7_PORT, 0);
	if ((tmp = mad_read(MC7_PORT)) != 0)
	{
		DDB(printk("MC7 not writable (%x)\n", tmp));
		return 0;
	}
	mad_write(MC7_PORT, 0xcb);
	if ((tmp = mad_read(MC7_PORT)) != 0xcb)
	{
		DDB(printk("MC7 not writable2 (%x)\n", tmp));
		return 0;
	}

	tmp = mad_read(MC0_PORT+18);
	if (tmp == 0xff || tmp == 0x00)
		return 1;
	/* We probably have a C931 */
	DDB(printk("Detected C931 config=0x%02x\n", tmp));
	c931_detected = 1;

	/*
         * We cannot configure the chip if it is in PnP mode.
         * If we have a CSN assigned (bit 8 in MC13) we first try
         * a software reset, then a software power off, finally
         * Clearing PnP mode. The last option is not
	 * Bit 8 in MC13 
         */
	if ((mad_read(MC0_PORT+13) & 0x80) == 0)
		return 1;

	/* Software reset */
	mad_write(MC9_PORT, 0x02);
	mad_write(MC9_PORT, 0x00);

	if ((mad_read(MC0_PORT+13) & 0x80) == 0)
		return 1;
	
	/* Power off, and on again */
	mad_write(MC9_PORT, 0xc2);
	mad_write(MC9_PORT, 0xc0);

	if ((mad_read(MC0_PORT+13) & 0x80) == 0)
		return 1;
	
#if 0	
	/* Force off PnP mode. This is not recommended because
	 * the PnP bios will not recognize the chip on the next
	 * warm boot and may assignd different resources to other
	 * PnP/PCI cards.
	 */
	mad_write(MC0_PORT+17, 0x04);
#endif
	return 1;
}

static int __init detect_mad16(void)
{
	unsigned char tmp, tmp2, bit;
	int i, port;

	/*
	 * Check that reading a register doesn't return bus float (0xff)
	 * when the card is accessed using password. This may fail in case
	 * the card is in low power mode. Normally at least the power saving
	 * mode bit should be 0.
	 */

	if ((tmp = mad_read(MC1_PORT)) == 0xff)
	{
		DDB(printk("MC1_PORT returned 0xff\n"));
		return 0;
	}
	for (i = 0xf8d; i <= 0xf98; i++)
		if (!c924pnp)
			DDB(printk("Port %0x (init value) = %0x\n", i, mad_read(i)));
		else
			DDB(printk("Port %0x (init value) = %0x\n", i-0x80, mad_read(i)));

	if (board_type == C930)
		return detect_c930();

	/*
	 * Now check that the gate is closed on first I/O after writing
	 * the password. (This is how a MAD16 compatible card works).
	 */

	if ((tmp2 = inb(MC1_PORT)) == tmp)	/* It didn't close */
	{
		DDB(printk("MC1_PORT didn't close after read (0x%02x)\n", tmp2));
		return 0;
	}

	bit  = (c924pnp) ?     0x20 : 0x80;
	port = (c924pnp) ? MC2_PORT : MC1_PORT;

	tmp = mad_read(port);
	mad_write(port, tmp ^ bit);	/* Toggle a bit */
	if ((tmp2 = mad_read(port)) != (tmp ^ bit))	/* Compare the bit */
	{
		mad_write(port, tmp);	/* Restore */
		DDB(printk("Bit revert test failed (0x%02x, 0x%02x)\n", tmp, tmp2));
		return 0;
	}
	mad_write(port, tmp);	/* Restore */
	return 1;		/* Bingo */
}

static int __init wss_init(struct address_info *hw_config)
{
	/*
	 * Check if the IO port returns valid signature. The original MS Sound
	 * system returns 0x04 while some cards (AudioTrix Pro for example)
	 * return 0x00.
	 */

	if ((inb(hw_config->io_base + 3) & 0x3f) != 0x04 &&
	    (inb(hw_config->io_base + 3) & 0x3f) != 0x00)
	{
		DDB(printk("No MSS signature detected on port 0x%x (0x%x)\n", hw_config->io_base, inb(hw_config->io_base + 3)));
		return 0;
	}
	/*
	 * Check that DMA0 is not in use with a 8 bit board.
	 */
	if (hw_config->dma == 0 && inb(hw_config->io_base + 3) & 0x80)
	{
		printk("MSS: Can't use DMA0 with a 8 bit card/slot\n");
		return 0;
	}
	if (hw_config->irq > 9 && inb(hw_config->io_base + 3) & 0x80)
		printk(KERN_ERR "MSS: Can't use IRQ%d with a 8 bit card/slot\n", hw_config->irq);
	return 1;
}

static void __init init_c930(struct address_info *hw_config, int base)
{
	unsigned char cfg = 0;

	cfg |= (0x0f & mad16_conf);

	if(c931_detected)
	{
		/* Bit 0 has reversd meaning. Bits 1 and 2 sese
		   reversed on write.
		   Support only IDE cdrom. IDE port programmed
		   somewhere else. */
		cfg =  (cfg & 0x09) ^ 0x07;
	}
	cfg |= base << 4;
	mad_write(MC1_PORT, cfg);

	/* MC2 is CD configuration. Don't touch it. */

	mad_write(MC3_PORT, 0);	/* Disable SB mode IRQ and DMA */

	/* bit 2 of MC4 reverses it's meaning between the C930
	   and the C931. */
	cfg = c931_detected ? 0x04 : 0x00;

	if(mad16_cdsel & 0x20)
		mad_write(MC4_PORT, 0x62|cfg);  /* opl4 */
	else
		mad_write(MC4_PORT, 0x52|cfg);  /* opl3 */

	mad_write(MC5_PORT, 0x3C);	/* Init it into mode2 */
	mad_write(MC6_PORT, 0x02);	/* Enable WSS, Disable MPU and SB */
	mad_write(MC7_PORT, 0xCB);
	mad_write(MC10_PORT, 0x11);
}

static int __init chip_detect(void)
{
	int i;

	/*
	 *    Then try to detect with the old password
	 */
	board_type = C924;

	DDB(printk("Detect using password = 0xE5\n"));
	
	if (detect_mad16()) {
		return 1;
	}
	
	board_type = C928;

	DDB(printk("Detect using password = 0xE2\n"));

	if (detect_mad16())
	{
		unsigned char model;

		if (((model = mad_read(MC3_PORT)) & 0x03) == 0x03) {
			DDB(printk("mad16.c: Mozart detected\n"));
			board_type = MOZART;
		} else {
			DDB(printk("mad16.c: 82C928 detected???\n"));
			board_type = C928;
		}
		return 1;
	}

	board_type = C929;

	DDB(printk("Detect using password = 0xE3\n"));

	if (detect_mad16())
	{
		DDB(printk("mad16.c: 82C929 detected\n"));
		return 1;
	}

	if (inb(PASSWD_REG) != 0xff)
		return 0;

	/*
	 * First relocate MC# registers to 0xe0e/0xe0f, disable password 
	 */

	outb((0xE4), PASSWD_REG);
	outb((0x80), PASSWD_REG);

	board_type = C930;

	DDB(printk("Detect using password = 0xE4\n"));

	for (i = 0xf8d; i <= 0xf93; i++)
		DDB(printk("port %03x = %02x\n", i, mad_read(i)));

        if(detect_mad16()) {
		DDB(printk("mad16.c: 82C930 detected\n"));
		return 1;
	}

	/* The C931 has the password reg at F8D */
	outb((0xE4), 0xF8D);
	outb((0x80), 0xF8D);
	DDB(printk("Detect using password = 0xE4 for C931\n"));

	if (detect_mad16()) {
		return 1;
	}

	board_type = C924;
	c924pnp++;
	DDB(printk("Detect using password = 0xE5 (again), port offset -0x80\n"));
	if (detect_mad16()) {
		DDB(printk("mad16.c: 82C924 PnP detected\n"));
		return 1;
	}
	
	c924pnp=0;

	return 0;
}

static int __init probe_mad16(struct address_info *hw_config)
{
	int i;
	unsigned char tmp;
	unsigned char cs4231_mode = 0;

	int ad_flags = 0;

	signed char bits;

	static char     dma_bits[4] = {
		1, 2, 0, 3
	};

	int config_port = hw_config->io_base + 0, version_port = hw_config->io_base + 3;
	int dma = hw_config->dma, dma2 = hw_config->dma2;
	unsigned char dma2_bit = 0;
	int base;
	struct resource *ports;

	mad16_osp = hw_config->osp;

	switch (hw_config->io_base) {
	case 0x530:
		base = 0;
		break;
	case 0xe80:
		base = 1;
		break;
	case 0xf40:
		base = 2;
		break;
	case 0x604:
		base = 3;
		break;
	default:
		printk(KERN_ERR "MAD16/Mozart: Bad WSS base address 0x%x\n", hw_config->io_base);
		return 0;
	}

	if (dma != 0 && dma != 1 && dma != 3) {
		printk(KERN_ERR "MSS: Bad DMA %d\n", dma);
		return 0;
	}

	/*
	 *    Check that all ports return 0xff (bus float) when no password
	 *      is written to the password register.
	 */

	DDB(printk("--- Detecting MAD16 / Mozart ---\n"));
	if (!chip_detect())
		return 0;

	switch (hw_config->irq) {
	case 7:
		bits = 8;
		break;
	case 9:
		bits = 0x10;
		break;
	case 10:
		bits = 0x18;
		break;
	case 12:
		bits = 0x20;
		break;
	case 5:	/* Also IRQ5 is possible on C930 */
		if (board_type == C930 || c924pnp) {
			bits = 0x28;
			break;
		}
	default:
		printk(KERN_ERR "MAD16/Mozart: Bad IRQ %d\n", hw_config->irq);
		return 0;
	}

	ports = request_region(hw_config->io_base + 4, 4, "ad1848");
	if (!ports) {
		printk(KERN_ERR "MSS: I/O port conflict\n");
		return 0;
	}
	if (!request_region(hw_config->io_base, 4, "mad16 WSS config")) {
		release_region(hw_config->io_base + 4, 4);
		printk(KERN_ERR "MSS: I/O port conflict\n");
		return 0;
	}

	if (board_type == C930) {
		init_c930(hw_config, base);
		goto got_it;
	}

	for (i = 0xf8d; i <= 0xf93; i++) {
		if (!c924pnp)
			DDB(printk("port %03x = %02x\n", i, mad_read(i)));
		else
			DDB(printk("port %03x = %02x\n", i-0x80, mad_read(i)));
	}

/*
 * Set the WSS address
 */

	tmp = (mad_read(MC1_PORT) & 0x0f) | 0x80;	/* Enable WSS, Disable SB */
	tmp |= base << 4;	/* WSS port select bits */

	/*
	 * Set optional CD-ROM and joystick settings.
	 */

	tmp &= ~0x0f;
	tmp |= (mad16_conf & 0x0f);	/* CD-ROM and joystick bits */
	mad_write(MC1_PORT, tmp);

	tmp = mad16_cdsel;
	mad_write(MC2_PORT, tmp);
	mad_write(MC3_PORT, 0xf0);	/* Disable SB */

	if (board_type == C924)	/* Specific C924 init values */
	{
		mad_write(MC4_PORT, 0xA0);
		mad_write(MC5_PORT, 0x05);
		mad_write(MC6_PORT, 0x03);
	}
	if (!ad1848_detect(ports, &ad_flags, mad16_osp))
		goto fail;

	if (ad_flags & (AD_F_CS4231 | AD_F_CS4248))
		cs4231_mode = 0x02;	/* CS4248/CS4231 sync delay switch */

	if (board_type == C929)
	{
		mad_write(MC4_PORT, 0xa2);
		mad_write(MC5_PORT, 0xA5 | cs4231_mode);
		mad_write(MC6_PORT, 0x03);	/* Disable MPU401 */
	}
	else
	{
		mad_write(MC4_PORT, 0x02);
		mad_write(MC5_PORT, 0x30 | cs4231_mode);
	}

	for (i = 0xf8d; i <= 0xf93; i++) {
		if (!c924pnp)
			DDB(printk("port %03x after init = %02x\n", i, mad_read(i)));
		else
			DDB(printk("port %03x after init = %02x\n", i-0x80, mad_read(i)));
	}

got_it:
	ad_flags = 0;
	if (!ad1848_detect(ports, &ad_flags, mad16_osp))
		goto fail;

	if (!wss_init(hw_config))
		goto fail;

	/*
	 * Set the IRQ and DMA addresses.
	 */
	
	outb((bits | 0x40), config_port);
	if ((inb(version_port) & 0x40) == 0)
		printk(KERN_ERR "[IRQ Conflict?]\n");

	/*
	 * Handle the capture DMA channel
	 */

	if (ad_flags & AD_F_CS4231 && dma2 != -1 && dma2 != dma)
	{
		if (!((dma == 0 && dma2 == 1) ||
			(dma == 1 && dma2 == 0) ||
			(dma == 3 && dma2 == 0)))
		{		/* Unsupported combination. Try to swap channels */
			int tmp = dma;

			dma = dma2;
			dma2 = tmp;
		}
		if ((dma == 0 && dma2 == 1) || (dma == 1 && dma2 == 0) ||
			(dma == 3 && dma2 == 0))
		{
			dma2_bit = 0x04;	/* Enable capture DMA */
		}
		else
		{
			printk("MAD16: Invalid capture DMA\n");
			dma2 = dma;
		}
	}
	else dma2 = dma;

	outb((bits | dma_bits[dma] | dma2_bit), config_port);	/* Write IRQ+DMA setup */

	hw_config->slots[0] = ad1848_init("mad16 WSS", ports,
					  hw_config->irq,
					  dma,
					  dma2, 0,
					  hw_config->osp,
					  THIS_MODULE);
	return 1;

fail:
	release_region(hw_config->io_base + 4, 4);
	release_region(hw_config->io_base, 4);
	return 0;
}

static int __init probe_mad16_mpu(struct address_info *hw_config)
{
	unsigned char tmp;

	if (board_type < C929)	/* Early chip. No MPU support. Just SB MIDI */
	{

#ifdef CONFIG_MAD16_OLDCARD

		tmp = mad_read(MC3_PORT);

		/* 
		 * MAD16 SB base is defined by the WSS base. It cannot be changed 
		 * alone.
		 * Ignore configured I/O base. Use the active setting. 
		 */

		if (mad_read(MC1_PORT) & 0x20)
			hw_config->io_base = 0x240;
		else
			hw_config->io_base = 0x220;

		switch (hw_config->irq)
		{
			case 5:
				tmp = (tmp & 0x3f) | 0x80;
				break;
			case 7:
				tmp = (tmp & 0x3f);
				break;
			case 11:
				tmp = (tmp & 0x3f) | 0x40;
				break;
			default:
				printk(KERN_ERR "mad16/Mozart: Invalid MIDI IRQ\n");
				return 0;
		}

		mad_write(MC3_PORT, tmp | 0x04);
		hw_config->driver_use_1 = SB_MIDI_ONLY;
		if (!request_region(hw_config->io_base, 16, "soundblaster"))
			return 0;
		if (!sb_dsp_detect(hw_config, 0, 0, NULL)) {
			release_region(hw_config->io_base, 16);
			return 0;
		}

		if (mad_read(MC1_PORT) & 0x20)
			hw_config->io_base = 0x240;
		else
			hw_config->io_base = 0x220;

		hw_config->name = "Mad16/Mozart";
		sb_dsp_init(hw_config, THIS_MODULE);
		return 1;
#else
		/* assuming all later Mozart cards are identified as
		 * either 82C928 or Mozart. If so, following code attempts
		 * to set MPU register. TODO - add probing
		 */

		tmp = mad_read(MC8_PORT);

		switch (hw_config->irq)
		{
			case 5:
				tmp |= 0x08;
				break;
			case 7:
				tmp |= 0x10;
				break;
			case 9:
				tmp |= 0x18;
				break;
			case 10:
				tmp |= 0x20;
				break;
			case 11:
				tmp |= 0x28;
				break;
			default:
				printk(KERN_ERR "mad16/MOZART: invalid mpu_irq\n");
				return 0;
		}

		switch (hw_config->io_base)
		{
			case 0x300:
				tmp |= 0x01;
				break;
			case 0x310:
				tmp |= 0x03;
				break;
			case 0x320:
				tmp |= 0x05;
				break;
			case 0x330:
				tmp |= 0x07;
				break;
			default:
				printk(KERN_ERR "mad16/MOZART: invalid mpu_io\n");
				return 0;
		}

		mad_write(MC8_PORT, tmp);	/* write MPU port parameters */
		goto probe_401;
#endif
	}
	tmp = mad_read(MC6_PORT) & 0x83;
	tmp |= 0x80;		/* MPU-401 enable */

	/* Set the MPU base bits */

	switch (hw_config->io_base)
	{
		case 0x300:
			tmp |= 0x60;
			break;
		case 0x310:
			tmp |= 0x40;
			break;
		case 0x320:
			tmp |= 0x20;
			break;
		case 0x330:
			tmp |= 0x00;
			break;
		default:
			printk(KERN_ERR "MAD16: Invalid MIDI port 0x%x\n", hw_config->io_base);
			return 0;
	}

	/* Set the MPU IRQ bits */

	switch (hw_config->irq)
	{
		case 5:
			tmp |= 0x10;
			break;
		case 7:
			tmp |= 0x18;
			break;
		case 9:
			tmp |= 0x00;
			break;
		case 10:
			tmp |= 0x08;
			break;
		default:
			printk(KERN_ERR "MAD16: Invalid MIDI IRQ %d\n", hw_config->irq);
			break;
	}
			
	mad_write(MC6_PORT, tmp);	/* Write MPU401 config */

#ifndef CONFIG_MAD16_OLDCARD
probe_401:
#endif
	hw_config->driver_use_1 = SB_MIDI_ONLY;
	hw_config->name = "Mad16/Mozart";
	return probe_uart401(hw_config, THIS_MODULE);
}

static void __exit unload_mad16(struct address_info *hw_config)
{
	ad1848_unload(hw_config->io_base + 4,
			hw_config->irq,
			hw_config->dma,
			hw_config->dma2, 0);
	release_region(hw_config->io_base, 4);
	sound_unload_audiodev(hw_config->slots[0]);
}

static void __exit unload_mad16_mpu(struct address_info *hw_config)
{
#ifdef CONFIG_MAD16_OLDCARD
	if (board_type < C929)	/* Early chip. No MPU support. Just SB MIDI */
	{
		sb_dsp_unload(hw_config, 0);
		return;
	}
#endif

	unload_uart401(hw_config);
}

static struct address_info cfg;
static struct address_info cfg_mpu;

static int found_mpu;

static int __initdata mpu_io = 0;
static int __initdata mpu_irq = 0;
static int __initdata io = -1;
static int __initdata dma = -1;
static int __initdata dma16 = -1; /* Set this for modules that need it */
static int __initdata irq = -1;
static int __initdata cdtype = 0;
static int __initdata cdirq = 0;
static int __initdata cdport = 0x340;
static int __initdata cddma = -1;
static int __initdata opl4 = 0;
static int __initdata joystick = 0;

module_param(mpu_io, int, 0);
module_param(mpu_irq, int, 0);
module_param(io, int, 0);
module_param(dma, int, 0);
module_param(dma16, int, 0);
module_param(irq, int, 0);
module_param(cdtype, int, 0);
module_param(cdirq, int, 0);
module_param(cdport, int, 0);
module_param(cddma, int, 0);
module_param(opl4, int, 0);
module_param(joystick, bool, 0);
module_param(debug, bool, 0644);

static int __initdata dma_map[2][8] =
{
	{0x03, -1, -1, -1, -1, 0x00, 0x01, 0x02},
	{0x03, -1, 0x01, 0x00, -1, -1, -1, -1}
};

static int __initdata irq_map[16] =
{
	0x00, -1, -1, 0x0A,
	-1, 0x04, -1, 0x08,
	-1, 0x10, 0x14, 0x18,
	-1, -1, -1, -1
};

#ifdef SUPPORT_JOYSTICK

static struct gameport *gameport;

static int __devinit mad16_register_gameport(int io_port)
{
	if (!request_region(io_port, 1, "mad16 gameport")) {
		printk(KERN_ERR "mad16: gameport address 0x%#x already in use\n", io_port);
		return -EBUSY;
	}

	gameport = gameport_allocate_port();
	if (!gameport) {
		printk(KERN_ERR "mad16: can not allocate memory for gameport\n");
		release_region(io_port, 1);
		return -ENOMEM;
	}

	gameport_set_name(gameport, "MAD16 Gameport");
	gameport_set_phys(gameport, "isa%04x/gameport0", io_port);
	gameport->io = io_port;

	gameport_register_port(gameport);

	return 0;
}

static inline void mad16_unregister_gameport(void)
{
	if (gameport) {
		/* the gameport was initialized so we must free it up */
		gameport_unregister_port(gameport);
		gameport = NULL;
		release_region(0x201, 1);
	}
}
#else
static inline int mad16_register_gameport(int io_port) { return -ENOSYS; }
static inline void mad16_unregister_gameport(void) { }
#endif

static int __devinit init_mad16(void)
{
	int dmatype = 0;

	printk(KERN_INFO "MAD16 audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	printk(KERN_INFO "CDROM ");
	switch (cdtype)
	{
		case 0x00:
			printk("Disabled");
			cdirq = 0;
			break;
		case 0x02:
			printk("Sony CDU31A");
			dmatype = 1;
			if(cddma == -1) cddma = 3;
			break;
		case 0x04:
			printk("Mitsumi");
			dmatype = 0;
			if(cddma == -1) cddma = 5;
			break;
		case 0x06:
			printk("Panasonic Lasermate");
			dmatype = 1;
			if(cddma == -1) cddma = 3;
			break;
		case 0x08:
			printk("Secondary IDE");
			dmatype = 0;
			if(cddma == -1) cddma = 5;
			break;
		case 0x0A:
			printk("Primary IDE");
			dmatype = 0;
			if(cddma == -1) cddma = 5;
			break;
		default:
			printk("\n");
			printk(KERN_ERR "Invalid CDROM type\n");
			return -EINVAL;
	}

	/*
	 *    Build the config words
	 */

	mad16_conf = (joystick ^ 1) | cdtype;
	mad16_cdsel = 0;
	if (opl4)
		mad16_cdsel |= 0x20;

	if(cdtype){
		if (cddma > 7 || cddma < 0 || dma_map[dmatype][cddma] == -1)
		{
			printk("\n");
			printk(KERN_ERR "Invalid CDROM DMA\n");
			return -EINVAL;
		}
		if (cddma)
			printk(", DMA %d", cddma);
		else
			printk(", no DMA");

		if (!cdirq)
			printk(", no IRQ");
		else if (cdirq < 0 || cdirq > 15 || irq_map[cdirq] == -1)
		{
			printk(", invalid IRQ (disabling)");
			cdirq = 0;
		}
		else printk(", IRQ %d", cdirq);

		mad16_cdsel |= dma_map[dmatype][cddma];

		if (cdtype < 0x08)
		{
			switch (cdport)
			{
				case 0x340:
					mad16_cdsel |= 0x00;
					break;
				case 0x330:
					mad16_cdsel |= 0x40;
					break;
				case 0x360:
					mad16_cdsel |= 0x80;
					break;
				case 0x320:
					mad16_cdsel |= 0xC0;
					break;
				default:
					printk(KERN_ERR "Unknown CDROM I/O base %d\n", cdport);
					return -EINVAL;
			}
		}
		mad16_cdsel |= irq_map[cdirq];
	}

	printk(".\n");

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma16;

	if (cfg.io_base == -1 || cfg.dma == -1 || cfg.irq == -1) {
		printk(KERN_ERR "I/O, DMA and irq are mandatory\n");
		return -EINVAL;
	}

	if (!request_region(MC0_PORT, 12, "mad16"))
		return -EBUSY;

	if (!probe_mad16(&cfg)) {
		release_region(MC0_PORT, 12);
		return -ENODEV;
	}

	cfg_mpu.io_base = mpu_io;
	cfg_mpu.irq = mpu_irq;

	found_mpu = probe_mad16_mpu(&cfg_mpu);

	if (joystick)
		mad16_register_gameport(0x201);

	return 0;
}

static void __exit cleanup_mad16(void)
{
	if (found_mpu)
		unload_mad16_mpu(&cfg_mpu);
	mad16_unregister_gameport();
	unload_mad16(&cfg);
	release_region(MC0_PORT, 12);
}

module_init(init_mad16);
module_exit(cleanup_mad16);

#ifndef MODULE
static int __init setup_mad16(char *str)
{
	/* io, irq */
	int ints[8];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	io	 = ints[1];
	irq	 = ints[2];
	dma	 = ints[3];
	dma16	 = ints[4];
	mpu_io	 = ints[5];
	mpu_irq  = ints[6];
	joystick = ints[7];

	return 1;
}

__setup("mad16=", setup_mad16);
#endif
MODULE_LICENSE("GPL");
