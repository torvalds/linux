/*
 * linux/sound/oss/waveartist.c
 *
 * The low level driver for the RWA010 Rockwell Wave Artist
 * codec chip used in the Rebel.com NetWinder.
 *
 * Cleaned up and integrated into 2.1 by Russell King (rmk@arm.linux.org.uk)
 * and Pat Beirne (patb@corel.ca)
 *
 *
 * Copyright (C) by Rebel.com 1998-1999
 *
 * RWA010 specs received under NDA from Rockwell
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 * 11-10-2000	Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
 *		Added __init to waveartist_init()
 */

/* Debugging */
#define DEBUG_CMD	1
#define DEBUG_OUT	2
#define DEBUG_IN	4
#define DEBUG_INTR	8
#define DEBUG_MIXER	16
#define DEBUG_TRIGGER	32

#define debug_flg (0)

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>

#include <asm/system.h>

#include "sound_config.h"
#include "waveartist.h"

#ifdef CONFIG_ARM
#include <mach/hardware.h>
#include <asm/mach-types.h>
#endif

#ifndef NO_DMA
#define NO_DMA	255
#endif

#define SUPPORTED_MIXER_DEVICES		(SOUND_MASK_SYNTH      |\
					 SOUND_MASK_PCM        |\
					 SOUND_MASK_LINE       |\
					 SOUND_MASK_MIC        |\
					 SOUND_MASK_LINE1      |\
					 SOUND_MASK_RECLEV     |\
					 SOUND_MASK_VOLUME     |\
					 SOUND_MASK_IMIX)

static unsigned short levels[SOUND_MIXER_NRDEVICES] = {
	0x5555,		/* Master Volume	 */
	0x0000,		/* Bass			 */
	0x0000,		/* Treble		 */
	0x2323,		/* Synth (FM)		 */
	0x4b4b,		/* PCM			 */
	0x6464,		/* PC Speaker		 */
	0x0000,		/* Ext Line		 */
	0x0000,		/* Mic			 */
	0x0000,		/* CD			 */
	0x6464,		/* Recording monitor	 */
	0x0000,		/* SB PCM (ALT PCM)	 */
	0x0000,		/* Recording level	 */
	0x6464,		/* Input gain		 */
	0x6464,		/* Output gain		 */
	0x0000,		/* Line1 (Aux1)		 */
	0x0000,		/* Line2 (Aux2)		 */
	0x0000,		/* Line3 (Aux3)		 */
	0x0000,		/* Digital1		 */
	0x0000,		/* Digital2		 */
	0x0000,		/* Digital3		 */
	0x0000,		/* Phone In		 */
	0x6464,		/* Phone Out		 */
	0x0000,		/* Video		 */
	0x0000,		/* Radio		 */
	0x0000		/* Monitor		 */
};

typedef struct {
	struct address_info  hw;	/* hardware */
	char		*chip_name;

	int		xfer_count;
	int		audio_mode;
	int		open_mode;
	int		audio_flags;
	int		record_dev;
	int		playback_dev;
	int		dev_no;

	/* Mixer parameters */
	const struct waveartist_mixer_info *mix;

	unsigned short	*levels;	   /* cache of volume settings   */
	int		recmask;	   /* currently enabled recording device! */

#ifdef CONFIG_ARCH_NETWINDER
	signed int	slider_vol;	   /* hardware slider volume     */
	unsigned int	handset_detect	:1;
	unsigned int	telephone_detect:1;
	unsigned int	no_autoselect	:1;/* handset/telephone autoselects a path */
	unsigned int	spkr_mute_state	:1;/* set by ioctl or autoselect */
	unsigned int	line_mute_state	:1;/* set by ioctl or autoselect */
	unsigned int	use_slider	:1;/* use slider setting for o/p vol */
#endif
} wavnc_info;

/*
 * This is the implementation specific mixer information.
 */
struct waveartist_mixer_info {
	unsigned int	supported_devs;	   /* Supported devices */
	unsigned int	recording_devs;	   /* Recordable devies */
	unsigned int	stereo_devs;	   /* Stereo devices	*/

	unsigned int	(*select_input)(wavnc_info *, unsigned int,
					unsigned char *, unsigned char *);
	int		(*decode_mixer)(wavnc_info *, int,
					unsigned char, unsigned char);
	int		(*get_mixer)(wavnc_info *, int);
};

typedef struct wavnc_port_info {
	int		open_mode;
	int		speed;
	int		channels;
	int		audio_format;
} wavnc_port_info;

static int		nr_waveartist_devs;
static wavnc_info	adev_info[MAX_AUDIO_DEV];
static DEFINE_SPINLOCK(waveartist_lock);

#ifndef CONFIG_ARCH_NETWINDER
#define machine_is_netwinder() 0
#else
static struct timer_list vnc_timer;
static void vnc_configure_mixer(wavnc_info *devc, unsigned int input_mask);
static int vnc_private_ioctl(int dev, unsigned int cmd, int __user *arg);
static void vnc_slider_tick(unsigned long data);
#endif

static inline void
waveartist_set_ctlr(struct address_info *hw, unsigned char clear, unsigned char set)
{
	unsigned int ctlr_port = hw->io_base + CTLR;

	clear = ~clear & inb(ctlr_port);

	outb(clear | set, ctlr_port);
}

/* Toggle IRQ acknowledge line
 */
static inline void
waveartist_iack(wavnc_info *devc)
{
	unsigned int ctlr_port = devc->hw.io_base + CTLR;
	int old_ctlr;

	old_ctlr = inb(ctlr_port) & ~IRQ_ACK;

	outb(old_ctlr | IRQ_ACK, ctlr_port);
	outb(old_ctlr, ctlr_port);
}

static inline int
waveartist_sleep(int timeout_ms)
{
	unsigned int timeout = timeout_ms * 10 * HZ / 100;

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		timeout = schedule_timeout(timeout);
	} while (timeout);

	return 0;
}

static int
waveartist_reset(wavnc_info *devc)
{
	struct address_info *hw = &devc->hw;
	unsigned int timeout, res = -1;

	waveartist_set_ctlr(hw, -1, RESET);
	waveartist_sleep(2);
	waveartist_set_ctlr(hw, RESET, 0);

	timeout = 500;
	do {
		mdelay(2);

		if (inb(hw->io_base + STATR) & CMD_RF) {
			res = inw(hw->io_base + CMDR);
			if (res == 0x55aa)
				break;
		}
	} while (--timeout);

	if (timeout == 0) {
		printk(KERN_WARNING "WaveArtist: reset timeout ");
		if (res != (unsigned int)-1)
			printk("(res=%04X)", res);
		printk("\n");
		return 1;
	}
	return 0;
}

/* Helper function to send and receive words
 * from WaveArtist.  It handles all the handshaking
 * and can send or receive multiple words.
 */
static int
waveartist_cmd(wavnc_info *devc,
		int nr_cmd, unsigned int *cmd,
		int nr_resp, unsigned int *resp)
{
	unsigned int io_base = devc->hw.io_base;
	unsigned int timed_out = 0;
	unsigned int i;

	if (debug_flg & DEBUG_CMD) {
		printk("waveartist_cmd: cmd=");

		for (i = 0; i < nr_cmd; i++)
			printk("%04X ", cmd[i]);

		printk("\n");
	}

	if (inb(io_base + STATR) & CMD_RF) {
		int old_data;

		/* flush the port
		 */

		old_data = inw(io_base + CMDR);

		if (debug_flg & DEBUG_CMD)
			printk("flushed %04X...", old_data);

		udelay(10);
	}

	for (i = 0; !timed_out && i < nr_cmd; i++) {
		int count;

		for (count = 5000; count; count--)
			if (inb(io_base + STATR) & CMD_WE)
				break;

		if (!count)
			timed_out = 1;
		else
			outw(cmd[i], io_base + CMDR);
	}

	for (i = 0; !timed_out && i < nr_resp; i++) {
		int count;

		for (count = 5000; count; count--)
			if (inb(io_base + STATR) & CMD_RF)
				break;

		if (!count)
			timed_out = 1;
		else
			resp[i] = inw(io_base + CMDR);
	}

	if (debug_flg & DEBUG_CMD) {
		if (!timed_out) {
			printk("waveartist_cmd: resp=");

			for (i = 0; i < nr_resp; i++)
				printk("%04X ", resp[i]);

			printk("\n");
		} else
			printk("waveartist_cmd: timed out\n");
	}

	return timed_out ? 1 : 0;
}

/*
 * Send one command word
 */
static inline int
waveartist_cmd1(wavnc_info *devc, unsigned int cmd)
{
	return waveartist_cmd(devc, 1, &cmd, 0, NULL);
}

/*
 * Send one command, receive one word
 */
static inline unsigned int
waveartist_cmd1_r(wavnc_info *devc, unsigned int cmd)
{
	unsigned int ret;

	waveartist_cmd(devc, 1, &cmd, 1, &ret);

	return ret;
}

/*
 * Send a double command, receive one
 * word (and throw it away)
 */
static inline int
waveartist_cmd2(wavnc_info *devc, unsigned int cmd, unsigned int arg)
{
	unsigned int vals[2];

	vals[0] = cmd;
	vals[1] = arg;

	return waveartist_cmd(devc, 2, vals, 1, vals);
}

/*
 * Send a triple command
 */
static inline int
waveartist_cmd3(wavnc_info *devc, unsigned int cmd,
		unsigned int arg1, unsigned int arg2)
{
	unsigned int vals[3];

	vals[0] = cmd;
	vals[1] = arg1;
	vals[2] = arg2;

	return waveartist_cmd(devc, 3, vals, 0, NULL);
}

static int
waveartist_getrev(wavnc_info *devc, char *rev)
{
	unsigned int temp[2];
	unsigned int cmd = WACMD_GETREV;

	waveartist_cmd(devc, 1, &cmd, 2, temp);

	rev[0] = temp[0] >> 8;
	rev[1] = temp[0] & 255;
	rev[2] = '\0';

	return temp[0];
}

static void waveartist_halt_output(int dev);
static void waveartist_halt_input(int dev);
static void waveartist_halt(int dev);
static void waveartist_trigger(int dev, int state);

static int
waveartist_open(int dev, int mode)
{
	wavnc_info	*devc;
	wavnc_port_info	*portc;
	unsigned long	flags;

	if (dev < 0 || dev >= num_audiodevs)
		return -ENXIO;

	devc  = (wavnc_info *) audio_devs[dev]->devc;
	portc = (wavnc_port_info *) audio_devs[dev]->portc;

	spin_lock_irqsave(&waveartist_lock, flags);
	if (portc->open_mode || (devc->open_mode & mode)) {
		spin_unlock_irqrestore(&waveartist_lock, flags);
		return -EBUSY;
	}

	devc->audio_mode  = 0;
	devc->open_mode  |= mode;
	portc->open_mode  = mode;
	waveartist_trigger(dev, 0);

	if (mode & OPEN_READ)
		devc->record_dev = dev;
	if (mode & OPEN_WRITE)
		devc->playback_dev = dev;
	spin_unlock_irqrestore(&waveartist_lock, flags);

	return 0;
}

static void
waveartist_close(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned long	flags;

	spin_lock_irqsave(&waveartist_lock, flags);

	waveartist_halt(dev);

	devc->audio_mode = 0;
	devc->open_mode &= ~portc->open_mode;
	portc->open_mode = 0;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_output_block(int dev, unsigned long buf, int __count, int intrflag)
{
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;
	unsigned int	count = __count; 

	if (debug_flg & DEBUG_OUT)
		printk("waveartist: output block, buf=0x%lx, count=0x%x...\n",
			buf, count);
	/*
	 * 16 bit data
	 */
	if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))
		count >>= 1;

	if (portc->channels > 1)
		count >>= 1;

	count -= 1;

	if (devc->audio_mode & PCM_ENABLE_OUTPUT &&
	    audio_devs[dev]->flags & DMA_AUTOMODE &&
	    intrflag &&
	    count == devc->xfer_count) {
		devc->audio_mode |= PCM_ENABLE_OUTPUT;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}

	spin_lock_irqsave(&waveartist_lock, flags);

	/*
	 * set sample count
	 */
	waveartist_cmd2(devc, WACMD_OUTPUTSIZE, count);

	devc->xfer_count = count;
	devc->audio_mode |= PCM_ENABLE_OUTPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_start_input(int dev, unsigned long buf, int __count, int intrflag)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;
	unsigned int	count = __count;

	if (debug_flg & DEBUG_IN)
		printk("waveartist: start input, buf=0x%lx, count=0x%x...\n",
			buf, count);

	if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
		count >>= 1;

	if (portc->channels > 1)
		count >>= 1;

	count -= 1;

	if (devc->audio_mode & PCM_ENABLE_INPUT &&
	    audio_devs[dev]->flags & DMA_AUTOMODE &&
	    intrflag &&
	    count == devc->xfer_count) {
		devc->audio_mode |= PCM_ENABLE_INPUT;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}

	spin_lock_irqsave(&waveartist_lock, flags);

	/*
	 * set sample count
	 */
	waveartist_cmd2(devc, WACMD_INPUTSIZE, count);

	devc->xfer_count = count;
	devc->audio_mode |= PCM_ENABLE_INPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static int
waveartist_ioctl(int dev, unsigned int cmd, void __user * arg)
{
	return -EINVAL;
}

static unsigned int
waveartist_get_speed(wavnc_port_info *portc)
{
	unsigned int speed;

	/*
	 * program the speed, channels, bits
	 */
	if (portc->speed == 8000)
		speed = 0x2E71;
	else if (portc->speed == 11025)
		speed = 0x4000;
	else if (portc->speed == 22050)
		speed = 0x8000;
	else if (portc->speed == 44100)
		speed = 0x0;
	else {
		/*
		 * non-standard - just calculate
		 */
		speed = portc->speed << 16;

		speed = (speed / 44100) & 65535;
	}

	return speed;
}

static unsigned int
waveartist_get_bits(wavnc_port_info *portc)
{
	unsigned int bits;

	if (portc->audio_format == AFMT_S16_LE)
		bits = 1;
	else if (portc->audio_format == AFMT_S8)
		bits = 0;
	else
		bits = 2;	//default AFMT_U8

	return bits;
}

static int
waveartist_prepare_for_input(int dev, int bsize, int bcount)
{
	unsigned long	flags;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned int	speed, bits;

	if (devc->audio_mode)
		return 0;

	speed = waveartist_get_speed(portc);
	bits  = waveartist_get_bits(portc);

	spin_lock_irqsave(&waveartist_lock, flags);

	if (waveartist_cmd2(devc, WACMD_INPUTFORMAT, bits))
		printk(KERN_WARNING "waveartist: error setting the "
		       "record format to %d\n", portc->audio_format);

	if (waveartist_cmd2(devc, WACMD_INPUTCHANNELS, portc->channels))
		printk(KERN_WARNING "waveartist: error setting record "
		       "to %d channels\n", portc->channels);

	/*
	 * write cmd SetSampleSpeedTimeConstant
	 */
	if (waveartist_cmd2(devc, WACMD_INPUTSPEED, speed))
		printk(KERN_WARNING "waveartist: error setting the record "
		       "speed to %dHz.\n", portc->speed);

	if (waveartist_cmd2(devc, WACMD_INPUTDMA, 1))
		printk(KERN_WARNING "waveartist: error setting the record "
		       "data path to 0x%X\n", 1);

	if (waveartist_cmd2(devc, WACMD_INPUTFORMAT, bits))
		printk(KERN_WARNING "waveartist: error setting the record "
		       "format to %d\n", portc->audio_format);

	devc->xfer_count = 0;
	spin_unlock_irqrestore(&waveartist_lock, flags);
	waveartist_halt_input(dev);

	if (debug_flg & DEBUG_INTR) {
		printk("WA CTLR reg: 0x%02X.\n",
		       inb(devc->hw.io_base + CTLR));
		printk("WA STAT reg: 0x%02X.\n",
		       inb(devc->hw.io_base + STATR));
		printk("WA IRQS reg: 0x%02X.\n",
		       inb(devc->hw.io_base + IRQSTAT));
	}

	return 0;
}

static int
waveartist_prepare_for_output(int dev, int bsize, int bcount)
{
	unsigned long	flags;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned int	speed, bits;

	/*
	 * program the speed, channels, bits
	 */
	speed = waveartist_get_speed(portc);
	bits  = waveartist_get_bits(portc);

	spin_lock_irqsave(&waveartist_lock, flags);

	if (waveartist_cmd2(devc, WACMD_OUTPUTSPEED, speed) &&
	    waveartist_cmd2(devc, WACMD_OUTPUTSPEED, speed))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "speed to %dHz.\n", portc->speed);

	if (waveartist_cmd2(devc, WACMD_OUTPUTCHANNELS, portc->channels))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "to %d channels\n", portc->channels);

	if (waveartist_cmd2(devc, WACMD_OUTPUTDMA, 0))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "data path to 0x%X\n", 0);

	if (waveartist_cmd2(devc, WACMD_OUTPUTFORMAT, bits))
		printk(KERN_WARNING "waveartist: error setting the playback "
		       "format to %d\n", portc->audio_format);

	devc->xfer_count = 0;
	spin_unlock_irqrestore(&waveartist_lock, flags);
	waveartist_halt_output(dev);

	if (debug_flg & DEBUG_INTR) {
		printk("WA CTLR reg: 0x%02X.\n",inb(devc->hw.io_base + CTLR));
		printk("WA STAT reg: 0x%02X.\n",inb(devc->hw.io_base + STATR));
		printk("WA IRQS reg: 0x%02X.\n",inb(devc->hw.io_base + IRQSTAT));
	}

	return 0;
}

static void
waveartist_halt(int dev)
{
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc;

	if (portc->open_mode & OPEN_WRITE)
		waveartist_halt_output(dev);

	if (portc->open_mode & OPEN_READ)
		waveartist_halt_input(dev);

	devc = (wavnc_info *) audio_devs[dev]->devc;
	devc->audio_mode = 0;
}

static void
waveartist_halt_input(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;

	spin_lock_irqsave(&waveartist_lock, flags);

	/*
	 * Stop capture
	 */
	waveartist_cmd1(devc, WACMD_INPUTSTOP);

	devc->audio_mode &= ~PCM_ENABLE_INPUT;

	/*
	 * Clear interrupt by toggling
	 * the IRQ_ACK bit in CTRL
	 */
	if (inb(devc->hw.io_base + STATR) & IRQ_REQ)
		waveartist_iack(devc);

//	devc->audio_mode &= ~PCM_ENABLE_INPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_halt_output(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;

	spin_lock_irqsave(&waveartist_lock, flags);

	waveartist_cmd1(devc, WACMD_OUTPUTSTOP);

	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

	/*
	 * Clear interrupt by toggling
	 * the IRQ_ACK bit in CTRL
	 */
	if (inb(devc->hw.io_base + STATR) & IRQ_REQ)
		waveartist_iack(devc);

//	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static void
waveartist_trigger(int dev, int state)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned long	flags;

	if (debug_flg & DEBUG_TRIGGER) {
		printk("wavnc: audio trigger ");
		if (state & PCM_ENABLE_INPUT)
			printk("in ");
		if (state & PCM_ENABLE_OUTPUT)
			printk("out");
		printk("\n");
	}

	spin_lock_irqsave(&waveartist_lock, flags);

	state &= devc->audio_mode;

	if (portc->open_mode & OPEN_READ &&
	    state & PCM_ENABLE_INPUT)
		/*
		 * enable ADC Data Transfer to PC
		 */
		waveartist_cmd1(devc, WACMD_INPUTSTART);

	if (portc->open_mode & OPEN_WRITE &&
	    state & PCM_ENABLE_OUTPUT)
		/*
		 * enable DAC data transfer from PC
		 */
		waveartist_cmd1(devc, WACMD_OUTPUTSTART);

	spin_unlock_irqrestore(&waveartist_lock, flags);
}

static int
waveartist_set_speed(int dev, int arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg <= 0)
		return portc->speed;

	if (arg < 5000)
		arg = 5000;
	if (arg > 44100)
		arg = 44100;

	portc->speed = arg;
	return portc->speed;

}

static short
waveartist_set_channels(int dev, short arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg != 1 && arg != 2)
		return portc->channels;

	portc->channels = arg;
	return arg;
}

static unsigned int
waveartist_set_bits(int dev, unsigned int arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg == 0)
		return portc->audio_format;

	if ((arg != AFMT_U8) && (arg != AFMT_S16_LE) && (arg != AFMT_S8))
		arg = AFMT_U8;

	portc->audio_format = arg;

	return arg;
}

static struct audio_driver waveartist_audio_driver = {
	.owner			= THIS_MODULE,
	.open			= waveartist_open,
	.close			= waveartist_close,
	.output_block		= waveartist_output_block,
	.start_input		= waveartist_start_input,
	.ioctl			= waveartist_ioctl,
	.prepare_for_input	= waveartist_prepare_for_input,
	.prepare_for_output	= waveartist_prepare_for_output,
	.halt_io		= waveartist_halt,
	.halt_input		= waveartist_halt_input,
	.halt_output		= waveartist_halt_output,
	.trigger		= waveartist_trigger,
	.set_speed		= waveartist_set_speed,
	.set_bits		= waveartist_set_bits,
	.set_channels		= waveartist_set_channels
};


static irqreturn_t
waveartist_intr(int irq, void *dev_id)
{
	wavnc_info *devc = dev_id;
	int	   irqstatus, status;

	spin_lock(&waveartist_lock);
	irqstatus = inb(devc->hw.io_base + IRQSTAT);
	status    = inb(devc->hw.io_base + STATR);

	if (debug_flg & DEBUG_INTR)
		printk("waveartist_intr: stat=%02x, irqstat=%02x\n",
		       status, irqstatus);

	if (status & IRQ_REQ)	/* Clear interrupt */
		waveartist_iack(devc);
	else
		printk(KERN_WARNING "waveartist: unexpected interrupt\n");

	if (irqstatus & 0x01) {
		int temp = 1;

		/* PCM buffer done
		 */
		if ((status & DMA0) && (devc->audio_mode & PCM_ENABLE_OUTPUT)) {
			DMAbuf_outputintr(devc->playback_dev, 1);
			temp = 0;
		}
		if ((status & DMA1) && (devc->audio_mode & PCM_ENABLE_INPUT)) {
			DMAbuf_inputintr(devc->record_dev);
			temp = 0;
		}
		if (temp)	//default:
			printk(KERN_WARNING "waveartist: Unknown interrupt\n");
	}
	if (irqstatus & 0x2)
		// We do not use SB mode natively...
		printk(KERN_WARNING "waveartist: Unexpected SB interrupt...\n");
	spin_unlock(&waveartist_lock);
	return IRQ_HANDLED;
}

/* -------------------------------------------------------------------------
 * Mixer stuff
 */
struct mix_ent {
	unsigned char	reg_l;
	unsigned char	reg_r;
	unsigned char	shift;
	unsigned char	max;
};

static const struct mix_ent mix_devs[SOUND_MIXER_NRDEVICES] = {
	{ 2, 6, 1,  7 }, /* SOUND_MIXER_VOLUME   */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_BASS     */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_TREBLE   */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_SYNTH    */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_PCM      */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_SPEAKER  */
	{ 0, 4, 6, 31 }, /* SOUND_MIXER_LINE     */
	{ 2, 6, 4,  3 }, /* SOUND_MIXER_MIC      */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_CD       */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_IMIX     */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_ALTPCM   */
#if 0
	{ 3, 7, 0, 10 }, /* SOUND_MIXER_RECLEV   */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_IGAIN    */
#else
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_RECLEV   */
	{ 3, 7, 0,  7 }, /* SOUND_MIXER_IGAIN    */
#endif
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_OGAIN    */
	{ 0, 4, 1, 31 }, /* SOUND_MIXER_LINE1    */
	{ 1, 5, 6, 31 }, /* SOUND_MIXER_LINE2    */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_LINE3    */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_DIGITAL1 */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_DIGITAL2 */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_DIGITAL3 */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_PHONEIN  */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_PHONEOUT */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_VIDEO    */
	{ 0, 0, 0,  0 }, /* SOUND_MIXER_RADIO    */
	{ 0, 0, 0,  0 }  /* SOUND_MIXER_MONITOR  */
};

static void
waveartist_mixer_update(wavnc_info *devc, int whichDev)
{
	unsigned int lev_left, lev_right;

	lev_left  = devc->levels[whichDev] & 0xff;
	lev_right = devc->levels[whichDev] >> 8;

	if (lev_left > 100)
		lev_left = 100;
	if (lev_right > 100)
		lev_right = 100;

#define SCALE(lev,max)	((lev) * (max) / 100)

	if (machine_is_netwinder() && whichDev == SOUND_MIXER_PHONEOUT)
		whichDev = SOUND_MIXER_VOLUME;

	if (mix_devs[whichDev].reg_l || mix_devs[whichDev].reg_r) {
		const struct mix_ent *mix = mix_devs + whichDev;
		unsigned int mask, left, right;

		mask = mix->max << mix->shift;
		lev_left  = SCALE(lev_left,  mix->max) << mix->shift;
		lev_right = SCALE(lev_right, mix->max) << mix->shift;

		/* read left setting */
		left  = waveartist_cmd1_r(devc, WACMD_GET_LEVEL |
					       mix->reg_l << 8);

		/* read right setting */
		right = waveartist_cmd1_r(devc, WACMD_GET_LEVEL |
						mix->reg_r << 8);

		left  = (left  & ~mask) | (lev_left  & mask);
		right = (right & ~mask) | (lev_right & mask);

		/* write left,right back */
		waveartist_cmd3(devc, WACMD_SET_MIXER, left, right);
	} else {
		switch(whichDev) {
		case SOUND_MIXER_PCM:
			waveartist_cmd3(devc, WACMD_SET_LEVEL,
					SCALE(lev_left,  32767),
					SCALE(lev_right, 32767));
			break;

		case SOUND_MIXER_SYNTH:
			waveartist_cmd3(devc, 0x0100 | WACMD_SET_LEVEL,
					SCALE(lev_left,  32767),
					SCALE(lev_right, 32767));
			break;
		}
	}
}

/*
 * Set the ADC MUX to the specified values.  We do NOT do any
 * checking of the values passed, since we assume that the
 * relevant *_select_input function has done that for us.
 */
static void
waveartist_set_adc_mux(wavnc_info *devc, char left_dev, char right_dev)
{
	unsigned int reg_08, reg_09;

	reg_08 = waveartist_cmd1_r(devc, WACMD_GET_LEVEL | 0x0800);
	reg_09 = waveartist_cmd1_r(devc, WACMD_GET_LEVEL | 0x0900);

	reg_08 = (reg_08 & ~0x3f) | right_dev << 3 | left_dev;

	waveartist_cmd3(devc, WACMD_SET_MIXER, reg_08, reg_09);
}

/*
 * Decode a recording mask into a mixer selection as follows:
 *
 *     OSS Source	WA Source	Actual source
 *  SOUND_MASK_IMIX	Mixer		Mixer output (same as AD1848)
 *  SOUND_MASK_LINE	Line		Line in
 *  SOUND_MASK_LINE1	Aux 1		Aux 1 in
 *  SOUND_MASK_LINE2	Aux 2		Aux 2 in
 *  SOUND_MASK_MIC	Mic		Microphone
 */
static unsigned int
waveartist_select_input(wavnc_info *devc, unsigned int recmask,
			unsigned char *dev_l, unsigned char *dev_r)
{
	unsigned int recdev = ADC_MUX_NONE;

	if (recmask & SOUND_MASK_IMIX) {
		recmask = SOUND_MASK_IMIX;
		recdev = ADC_MUX_MIXER;
	} else if (recmask & SOUND_MASK_LINE2) {
		recmask = SOUND_MASK_LINE2;
		recdev = ADC_MUX_AUX2;
	} else if (recmask & SOUND_MASK_LINE1) {
		recmask = SOUND_MASK_LINE1;
		recdev = ADC_MUX_AUX1;
	} else if (recmask & SOUND_MASK_LINE) {
		recmask = SOUND_MASK_LINE;
		recdev = ADC_MUX_LINE;
	} else if (recmask & SOUND_MASK_MIC) {
		recmask = SOUND_MASK_MIC;
		recdev = ADC_MUX_MIC;
	}

	*dev_l = *dev_r = recdev;

	return recmask;
}

static int
waveartist_decode_mixer(wavnc_info *devc, int dev, unsigned char lev_l,
			unsigned char lev_r)
{
	switch (dev) {
	case SOUND_MIXER_VOLUME:
	case SOUND_MIXER_SYNTH:
	case SOUND_MIXER_PCM:
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_MIC:
	case SOUND_MIXER_IGAIN:
	case SOUND_MIXER_LINE1:
	case SOUND_MIXER_LINE2:
		devc->levels[dev] = lev_l | lev_r << 8;
		break;

	case SOUND_MIXER_IMIX:
		break;

	default:
		dev = -EINVAL;
		break;
	}

	return dev;
}

static int waveartist_get_mixer(wavnc_info *devc, int dev)
{
	return devc->levels[dev];
}

static const struct waveartist_mixer_info waveartist_mixer = {
	.supported_devs	= SUPPORTED_MIXER_DEVICES | SOUND_MASK_IGAIN,
	.recording_devs	= SOUND_MASK_LINE  | SOUND_MASK_MIC   |
			SOUND_MASK_LINE1 | SOUND_MASK_LINE2 |
			SOUND_MASK_IMIX,
	.stereo_devs	= (SUPPORTED_MIXER_DEVICES | SOUND_MASK_IGAIN) & ~
			(SOUND_MASK_SPEAKER | SOUND_MASK_IMIX),
	.select_input	= waveartist_select_input,
	.decode_mixer	= waveartist_decode_mixer,
	.get_mixer	= waveartist_get_mixer,
};

static void
waveartist_set_recmask(wavnc_info *devc, unsigned int recmask)
{
	unsigned char dev_l, dev_r;

	recmask &= devc->mix->recording_devs;

	/*
	 * If more than one recording device selected,
	 * disable the device that is currently in use.
	 */
	if (hweight32(recmask) > 1)
		recmask &= ~devc->recmask;

	/*
	 * Translate the recording device mask into
	 * the ADC multiplexer settings.
	 */
	devc->recmask = devc->mix->select_input(devc, recmask,
						&dev_l, &dev_r);

	waveartist_set_adc_mux(devc, dev_l, dev_r);
}

static int
waveartist_set_mixer(wavnc_info *devc, int dev, unsigned int level)
{
	unsigned int lev_left  = level & 0x00ff;
	unsigned int lev_right = (level & 0xff00) >> 8;

	if (lev_left > 100)
		lev_left = 100;
	if (lev_right > 100)
		lev_right = 100;

	/*
	 * Mono devices have their right volume forced to their
	 * left volume.  (from ALSA driver OSS emulation).
	 */
	if (!(devc->mix->stereo_devs & (1 << dev)))
		lev_right = lev_left;

	dev = devc->mix->decode_mixer(devc, dev, lev_left, lev_right);

	if (dev >= 0)
		waveartist_mixer_update(devc, dev);

	return dev < 0 ? dev : 0;
}

static int
waveartist_mixer_ioctl(int dev, unsigned int cmd, void __user * arg)
{
	wavnc_info *devc = (wavnc_info *)audio_devs[dev]->devc;
	int ret = 0, val, nr;

	/*
	 * All SOUND_MIXER_* ioctls use type 'M'
	 */
	if (((cmd >> 8) & 255) != 'M')
		return -ENOIOCTLCMD;

#ifdef CONFIG_ARCH_NETWINDER
	if (machine_is_netwinder()) {
		ret = vnc_private_ioctl(dev, cmd, arg);
		if (ret != -ENOIOCTLCMD)
			return ret;
		else
			ret = 0;
	}
#endif

	nr = cmd & 0xff;

	if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
		if (get_user(val, (int __user *)arg))
			return -EFAULT;

		switch (nr) {
		case SOUND_MIXER_RECSRC:
			waveartist_set_recmask(devc, val);
			break;

		default:
			ret = -EINVAL;
			if (nr < SOUND_MIXER_NRDEVICES &&
			    devc->mix->supported_devs & (1 << nr))
				ret = waveartist_set_mixer(devc, nr, val);
		}
	}

	if (ret == 0 && _SIOC_DIR(cmd) & _SIOC_READ) {
		ret = -EINVAL;

		switch (nr) {
		case SOUND_MIXER_RECSRC:
			ret = devc->recmask;
			break;

		case SOUND_MIXER_DEVMASK:
			ret = devc->mix->supported_devs;
			break;

		case SOUND_MIXER_STEREODEVS:
			ret = devc->mix->stereo_devs;
			break;

		case SOUND_MIXER_RECMASK:
			ret = devc->mix->recording_devs;
			break;

		case SOUND_MIXER_CAPS:
			ret = SOUND_CAP_EXCL_INPUT;
			break;

		default:
			if (nr < SOUND_MIXER_NRDEVICES)
				ret = devc->mix->get_mixer(devc, nr);
			break;
		}

		if (ret >= 0)
			ret = put_user(ret, (int __user *)arg) ? -EFAULT : 0;
	}

	return ret;
}

static struct mixer_operations waveartist_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "WaveArtist",
	.name	= "WaveArtist",
	.ioctl	= waveartist_mixer_ioctl
};

static void
waveartist_mixer_reset(wavnc_info *devc)
{
	int i;

	if (debug_flg & DEBUG_MIXER)
		printk("%s: mixer_reset\n", devc->hw.name);

	/*
	 * reset mixer cmd
	 */
	waveartist_cmd1(devc, WACMD_RST_MIXER);

	/*
	 * set input for ADC to come from 'quiet'
	 * turn on default modes
	 */
	waveartist_cmd3(devc, WACMD_SET_MIXER, 0x9800, 0xa836);

	/*
	 * set mixer input select to none, RX filter gains 0 dB
	 */
	waveartist_cmd3(devc, WACMD_SET_MIXER, 0x4c00, 0x8c00);

	/*
	 * set bit 0 reg 2 to 1 - unmute MonoOut
	 */
	waveartist_cmd3(devc, WACMD_SET_MIXER, 0x2801, 0x6800);

	/* set default input device = internal mic
	 * current recording device = none
	 */
	waveartist_set_recmask(devc, 0);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		waveartist_mixer_update(devc, i);
}

static int __init waveartist_init(wavnc_info *devc)
{
	wavnc_port_info *portc;
	char rev[3], dev_name[64];
	int my_dev;

	if (waveartist_reset(devc))
		return -ENODEV;

	sprintf(dev_name, "%s (%s", devc->hw.name, devc->chip_name);

	if (waveartist_getrev(devc, rev)) {
		strcat(dev_name, " rev. ");
		strcat(dev_name, rev);
	}
	strcat(dev_name, ")");

	conf_printf2(dev_name, devc->hw.io_base, devc->hw.irq,
		     devc->hw.dma, devc->hw.dma2);

	portc = kzalloc(sizeof(wavnc_port_info), GFP_KERNEL);
	if (portc == NULL)
		goto nomem;

	my_dev = sound_install_audiodrv(AUDIO_DRIVER_VERSION, dev_name,
			&waveartist_audio_driver, sizeof(struct audio_driver),
			devc->audio_flags, AFMT_U8 | AFMT_S16_LE | AFMT_S8,
			devc, devc->hw.dma, devc->hw.dma2);

	if (my_dev < 0)
		goto free;

	audio_devs[my_dev]->portc = portc;

	waveartist_mixer_reset(devc);

	/*
	 * clear any pending interrupt
	 */
	waveartist_iack(devc);

	if (request_irq(devc->hw.irq, waveartist_intr, 0, devc->hw.name, devc) < 0) {
		printk(KERN_ERR "%s: IRQ %d in use\n",
			devc->hw.name, devc->hw.irq);
		goto uninstall;
	}

	if (sound_alloc_dma(devc->hw.dma, devc->hw.name)) {
		printk(KERN_ERR "%s: Can't allocate DMA%d\n",
			devc->hw.name, devc->hw.dma);
		goto uninstall_irq;
	}

	if (devc->hw.dma != devc->hw.dma2 && devc->hw.dma2 != NO_DMA)
		if (sound_alloc_dma(devc->hw.dma2, devc->hw.name)) {
			printk(KERN_ERR "%s: can't allocate DMA%d\n",
				devc->hw.name, devc->hw.dma2);
			goto uninstall_dma;
		}

	waveartist_set_ctlr(&devc->hw, 0, DMA1_IE | DMA0_IE);

	audio_devs[my_dev]->mixer_dev =
		sound_install_mixer(MIXER_DRIVER_VERSION,
				dev_name,
				&waveartist_mixer_operations,
				sizeof(struct mixer_operations),
				devc);

	return my_dev;

uninstall_dma:
	sound_free_dma(devc->hw.dma);

uninstall_irq:
	free_irq(devc->hw.irq, devc);

uninstall:
	sound_unload_audiodev(my_dev);

free:
	kfree(portc);

nomem:
	return -1;
}

static int __init probe_waveartist(struct address_info *hw_config)
{
	wavnc_info *devc = &adev_info[nr_waveartist_devs];

	if (nr_waveartist_devs >= MAX_AUDIO_DEV) {
		printk(KERN_WARNING "waveartist: too many audio devices\n");
		return 0;
	}

	if (!request_region(hw_config->io_base, 15, hw_config->name))  {
		printk(KERN_WARNING "WaveArtist: I/O port conflict\n");
		return 0;
	}

	if (hw_config->irq > 15 || hw_config->irq < 0) {
		release_region(hw_config->io_base, 15);
		printk(KERN_WARNING "WaveArtist: Bad IRQ %d\n",
		       hw_config->irq);
		return 0;
	}

	if (hw_config->dma != 3) {
		release_region(hw_config->io_base, 15);
		printk(KERN_WARNING "WaveArtist: Bad DMA %d\n",
		       hw_config->dma);
		return 0;
	}

	hw_config->name = "WaveArtist";
	devc->hw = *hw_config;
	devc->open_mode = 0;
	devc->chip_name = "RWA-010";

	return 1;
}

static void __init
attach_waveartist(struct address_info *hw, const struct waveartist_mixer_info *mix)
{
	wavnc_info *devc = &adev_info[nr_waveartist_devs];

	/*
	 * NOTE! If irq < 0, there is another driver which has allocated the
	 *   IRQ so that this driver doesn't need to allocate/deallocate it.
	 *   The actually used IRQ is ABS(irq).
	 */
	devc->hw = *hw;
	devc->hw.irq = (hw->irq > 0) ? hw->irq : 0;
	devc->open_mode = 0;
	devc->playback_dev = 0;
	devc->record_dev = 0;
	devc->audio_flags = DMA_AUTOMODE;
	devc->levels = levels;

	if (hw->dma != hw->dma2 && hw->dma2 != NO_DMA)
		devc->audio_flags |= DMA_DUPLEX;

	devc->mix = mix;
	devc->dev_no = waveartist_init(devc);

	if (devc->dev_no < 0)
		release_region(hw->io_base, 15);
	else {
#ifdef CONFIG_ARCH_NETWINDER
		if (machine_is_netwinder()) {
			init_timer(&vnc_timer);
			vnc_timer.function = vnc_slider_tick;
			vnc_timer.expires  = jiffies;
			vnc_timer.data     = nr_waveartist_devs;
			add_timer(&vnc_timer);

			vnc_configure_mixer(devc, 0);

			devc->no_autoselect = 1;
		}
#endif
		nr_waveartist_devs += 1;
	}
}

static void __exit unload_waveartist(struct address_info *hw)
{
	wavnc_info *devc = NULL;
	int i;

	for (i = 0; i < nr_waveartist_devs; i++)
		if (hw->io_base == adev_info[i].hw.io_base) {
			devc = adev_info + i;
			break;
		}

	if (devc != NULL) {
		int mixer;

#ifdef CONFIG_ARCH_NETWINDER
		if (machine_is_netwinder())
			del_timer(&vnc_timer);
#endif

		release_region(devc->hw.io_base, 15);

		waveartist_set_ctlr(&devc->hw, DMA1_IE|DMA0_IE, 0);

		if (devc->hw.irq >= 0)
			free_irq(devc->hw.irq, devc);

		sound_free_dma(devc->hw.dma);

		if (devc->hw.dma != devc->hw.dma2 &&
		    devc->hw.dma2 != NO_DMA)
			sound_free_dma(devc->hw.dma2);

		mixer = audio_devs[devc->dev_no]->mixer_dev;

		if (mixer >= 0)
			sound_unload_mixerdev(mixer);

		if (devc->dev_no >= 0)
			sound_unload_audiodev(devc->dev_no);

		nr_waveartist_devs -= 1;

		for (; i < nr_waveartist_devs; i++)
			adev_info[i] = adev_info[i + 1];
	} else
		printk(KERN_WARNING "waveartist: can't find device "
		       "to unload\n");
}

#ifdef CONFIG_ARCH_NETWINDER

/*
 * Rebel.com Netwinder specifics...
 */

#include <asm/hardware/dec21285.h>
 
#define	VNC_TIMER_PERIOD (HZ/4)	//check slider 4 times/sec

#define	MIXER_PRIVATE3_RESET	0x53570000
#define	MIXER_PRIVATE3_READ	0x53570001
#define	MIXER_PRIVATE3_WRITE	0x53570002

#define	VNC_MUTE_INTERNAL_SPKR	0x01	//the sw mute on/off control bit
#define	VNC_MUTE_LINE_OUT	0x10
#define VNC_PHONE_DETECT	0x20
#define VNC_HANDSET_DETECT	0x40
#define VNC_DISABLE_AUTOSWITCH	0x80

static inline void
vnc_mute_spkr(wavnc_info *devc)
{
	unsigned long flags;

	spin_lock_irqsave(&nw_gpio_lock, flags);
	nw_cpld_modify(CPLD_UNMUTE, devc->spkr_mute_state ? 0 : CPLD_UNMUTE);
	spin_unlock_irqrestore(&nw_gpio_lock, flags);
}

static void
vnc_mute_lout(wavnc_info *devc)
{
	unsigned int left, right;

	left  = waveartist_cmd1_r(devc, WACMD_GET_LEVEL);
	right = waveartist_cmd1_r(devc, WACMD_GET_LEVEL | 0x400);

	if (devc->line_mute_state) {
		left &= ~1;
		right &= ~1;
	} else {
		left |= 1;
		right |= 1;
	}
	waveartist_cmd3(devc, WACMD_SET_MIXER, left, right);
		
}

static int
vnc_volume_slider(wavnc_info *devc)
{
	static signed int old_slider_volume;
	unsigned long flags;
	signed int volume = 255;

	*CSR_TIMER1_LOAD = 0x00ffffff;

	spin_lock_irqsave(&waveartist_lock, flags);

	outb(0xFF, 0x201);
	*CSR_TIMER1_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_DIV1;

	while (volume && (inb(0x201) & 0x01))
		volume--;

	*CSR_TIMER1_CNTL = 0;

	spin_unlock_irqrestore(&waveartist_lock,flags);
	
	volume = 0x00ffffff - *CSR_TIMER1_VALUE;


#ifndef REVERSE
	volume = 150 - (volume >> 5);
#else
	volume = (volume >> 6) - 25;
#endif

	if (volume < 0)
		volume = 0;

	if (volume > 100)
		volume = 100;

	/*
	 * slider quite often reads +-8, so debounce this random noise
	 */
	if (abs(volume - old_slider_volume) > 7) {
		old_slider_volume = volume;

		if (debug_flg & DEBUG_MIXER)
			printk(KERN_DEBUG "Slider volume: %d.\n", volume);
	}

	return old_slider_volume;
}

/*
 * Decode a recording mask into a mixer selection on the NetWinder
 * as follows:
 *
 *     OSS Source	WA Source	Actual source
 *  SOUND_MASK_IMIX	Mixer		Mixer output (same as AD1848)
 *  SOUND_MASK_LINE	Line		Line in
 *  SOUND_MASK_LINE1	Left Mic	Handset
 *  SOUND_MASK_PHONEIN	Left Aux	Telephone microphone
 *  SOUND_MASK_MIC	Right Mic	Builtin microphone
 */
static unsigned int
netwinder_select_input(wavnc_info *devc, unsigned int recmask,
		       unsigned char *dev_l, unsigned char *dev_r)
{
	unsigned int recdev_l = ADC_MUX_NONE, recdev_r = ADC_MUX_NONE;

	if (recmask & SOUND_MASK_IMIX) {
		recmask = SOUND_MASK_IMIX;
		recdev_l = ADC_MUX_MIXER;
		recdev_r = ADC_MUX_MIXER;
	} else if (recmask & SOUND_MASK_LINE) {
		recmask = SOUND_MASK_LINE;
		recdev_l = ADC_MUX_LINE;
		recdev_r = ADC_MUX_LINE;
	} else if (recmask & SOUND_MASK_LINE1) {
		recmask = SOUND_MASK_LINE1;
		waveartist_cmd1(devc, WACMD_SET_MONO); /* left */
		recdev_l = ADC_MUX_MIC;
		recdev_r = ADC_MUX_NONE;
	} else if (recmask & SOUND_MASK_PHONEIN) {
		recmask = SOUND_MASK_PHONEIN;
		waveartist_cmd1(devc, WACMD_SET_MONO); /* left */
		recdev_l = ADC_MUX_AUX1;
		recdev_r = ADC_MUX_NONE;
	} else if (recmask & SOUND_MASK_MIC) {
		recmask = SOUND_MASK_MIC;
		waveartist_cmd1(devc, WACMD_SET_MONO | 0x100);	/* right */
		recdev_l = ADC_MUX_NONE;
		recdev_r = ADC_MUX_MIC;
	}

	*dev_l = recdev_l;
	*dev_r = recdev_r;

	return recmask;
}

static int
netwinder_decode_mixer(wavnc_info *devc, int dev, unsigned char lev_l,
		       unsigned char lev_r)
{
	switch (dev) {
	case SOUND_MIXER_VOLUME:
	case SOUND_MIXER_SYNTH:
	case SOUND_MIXER_PCM:
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_IGAIN:
		devc->levels[dev] = lev_l | lev_r << 8;
		break;

	case SOUND_MIXER_MIC:		/* right mic only */
		devc->levels[SOUND_MIXER_MIC] &= 0xff;
		devc->levels[SOUND_MIXER_MIC] |= lev_l << 8;
		break;

	case SOUND_MIXER_LINE1:		/* left mic only  */
		devc->levels[SOUND_MIXER_MIC] &= 0xff00;
		devc->levels[SOUND_MIXER_MIC] |= lev_l;
		dev = SOUND_MIXER_MIC;
		break;

	case SOUND_MIXER_PHONEIN:	/* left aux only  */
		devc->levels[SOUND_MIXER_LINE1] = lev_l;
		dev = SOUND_MIXER_LINE1;
		break;

	case SOUND_MIXER_IMIX:
	case SOUND_MIXER_PHONEOUT:
		break;

	default:
		dev = -EINVAL;
		break;
	}
	return dev;
}

static int netwinder_get_mixer(wavnc_info *devc, int dev)
{
	int levels;

	switch (dev) {
	case SOUND_MIXER_VOLUME:
	case SOUND_MIXER_SYNTH:
	case SOUND_MIXER_PCM:
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_IGAIN:
		levels = devc->levels[dev];
		break;

	case SOUND_MIXER_MIC:		/* builtin mic: right mic only */
		levels = devc->levels[SOUND_MIXER_MIC] >> 8;
		levels |= levels << 8;
		break;

	case SOUND_MIXER_LINE1:		/* handset mic: left mic only */
		levels = devc->levels[SOUND_MIXER_MIC] & 0xff;
		levels |= levels << 8;
		break;

	case SOUND_MIXER_PHONEIN:	/* phone mic: left aux1 only */
		levels = devc->levels[SOUND_MIXER_LINE1] & 0xff;
		levels |= levels << 8;
		break;

	default:
		levels = 0;
	}

	return levels;
}

/*
 * Waveartist specific mixer information.
 */
static const struct waveartist_mixer_info netwinder_mixer = {
	.supported_devs	= SOUND_MASK_VOLUME  | SOUND_MASK_SYNTH   |
			SOUND_MASK_PCM     | SOUND_MASK_SPEAKER |
			SOUND_MASK_LINE    | SOUND_MASK_MIC     |
			SOUND_MASK_IMIX    | SOUND_MASK_LINE1   |
			SOUND_MASK_PHONEIN | SOUND_MASK_PHONEOUT|
			SOUND_MASK_IGAIN,

	.recording_devs	= SOUND_MASK_LINE    | SOUND_MASK_MIC     |
			SOUND_MASK_IMIX    | SOUND_MASK_LINE1   |
			SOUND_MASK_PHONEIN,

	.stereo_devs	= SOUND_MASK_VOLUME  | SOUND_MASK_SYNTH   |
			SOUND_MASK_PCM     | SOUND_MASK_LINE    |
			SOUND_MASK_IMIX    | SOUND_MASK_IGAIN,

	.select_input	= netwinder_select_input,
	.decode_mixer	= netwinder_decode_mixer,
	.get_mixer	= netwinder_get_mixer,
};

static void
vnc_configure_mixer(wavnc_info *devc, unsigned int recmask)
{
	if (!devc->no_autoselect) {
		if (devc->handset_detect) {
			recmask = SOUND_MASK_LINE1;
			devc->spkr_mute_state = devc->line_mute_state = 1;
		} else if (devc->telephone_detect) {
			recmask = SOUND_MASK_PHONEIN;
			devc->spkr_mute_state = devc->line_mute_state = 1;
		} else {
			/* unless someone has asked for LINE-IN,
			 * we default to MIC
			 */
			if ((devc->recmask & SOUND_MASK_LINE) == 0)
				devc->recmask = SOUND_MASK_MIC;
			devc->spkr_mute_state = devc->line_mute_state = 0;
		}
		vnc_mute_spkr(devc);
		vnc_mute_lout(devc);

		if (recmask != devc->recmask)
			waveartist_set_recmask(devc, recmask);
	}
}

static int
vnc_slider(wavnc_info *devc)
{
	signed int slider_volume;
	unsigned int temp, old_hs, old_td;

	/*
	 * read the "buttons" state.
	 *  Bit 4 = 0 means handset present
	 *  Bit 5 = 1 means phone offhook
	 */
	temp = inb(0x201);

	old_hs = devc->handset_detect;
	old_td = devc->telephone_detect;

	devc->handset_detect = !(temp & 0x10);
	devc->telephone_detect = !!(temp & 0x20);

	if (!devc->no_autoselect &&
	    (old_hs != devc->handset_detect ||
	     old_td != devc->telephone_detect))
		vnc_configure_mixer(devc, devc->recmask);

	slider_volume = vnc_volume_slider(devc);

	/*
	 * If we're using software controlled volume, and
	 * the slider moves by more than 20%, then we
	 * switch back to slider controlled volume.
	 */
	if (abs(devc->slider_vol - slider_volume) > 20)
		devc->use_slider = 1;

	/*
	 * use only left channel
	 */
	temp = levels[SOUND_MIXER_VOLUME] & 0xFF;

	if (slider_volume != temp && devc->use_slider) {
		devc->slider_vol = slider_volume;

		waveartist_set_mixer(devc, SOUND_MIXER_VOLUME,
			slider_volume | slider_volume << 8);

		return 1;
	}

	return 0;
}

static void
vnc_slider_tick(unsigned long data)
{
	int next_timeout;

	if (vnc_slider(adev_info + data))
		next_timeout = 5;	// mixer reported change
	else
		next_timeout = VNC_TIMER_PERIOD;

	mod_timer(&vnc_timer, jiffies + next_timeout);
}

static int
vnc_private_ioctl(int dev, unsigned int cmd, int __user * arg)
{
	wavnc_info *devc = (wavnc_info *)audio_devs[dev]->devc;
	int val;

	switch (cmd) {
	case SOUND_MIXER_PRIVATE1:
	{
		u_int prev_spkr_mute, prev_line_mute, prev_auto_state;
		int val;

		if (get_user(val, arg))
			return -EFAULT;

		/* check if parameter is logical */
		if (val & ~(VNC_MUTE_INTERNAL_SPKR |
			    VNC_MUTE_LINE_OUT |
			    VNC_DISABLE_AUTOSWITCH))
			return -EINVAL;

		prev_auto_state = devc->no_autoselect;
		prev_spkr_mute  = devc->spkr_mute_state;
		prev_line_mute  = devc->line_mute_state;

		devc->no_autoselect   = (val & VNC_DISABLE_AUTOSWITCH) ? 1 : 0;
		devc->spkr_mute_state = (val & VNC_MUTE_INTERNAL_SPKR) ? 1 : 0;
		devc->line_mute_state = (val & VNC_MUTE_LINE_OUT) ? 1 : 0;

		if (prev_spkr_mute != devc->spkr_mute_state)
			vnc_mute_spkr(devc);

		if (prev_line_mute != devc->line_mute_state)
			vnc_mute_lout(devc);

		if (prev_auto_state != devc->no_autoselect)
			vnc_configure_mixer(devc, devc->recmask);

		return 0;
	}

	case SOUND_MIXER_PRIVATE2:
		if (get_user(val, arg))
			return -EFAULT;

		switch (val) {
#define VNC_SOUND_PAUSE         0x53    //to pause the DSP
#define VNC_SOUND_RESUME        0x57    //to unpause the DSP
		case VNC_SOUND_PAUSE:
			waveartist_cmd1(devc, 0x16);
			break;

		case VNC_SOUND_RESUME:
			waveartist_cmd1(devc, 0x18);
			break;

		default:
			return -EINVAL;
		}
		return 0;

	/* private ioctl to allow bulk access to waveartist */
	case SOUND_MIXER_PRIVATE3:
	{
		unsigned long	flags;
		int		mixer_reg[15], i, val;

		if (get_user(val, arg))
			return -EFAULT;
		if (copy_from_user(mixer_reg, (void *)val, sizeof(mixer_reg)))
			return -EFAULT;

		switch (mixer_reg[14]) {
		case MIXER_PRIVATE3_RESET:
			waveartist_mixer_reset(devc);
			break;

		case MIXER_PRIVATE3_WRITE:
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[0], mixer_reg[4]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[1], mixer_reg[5]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[2], mixer_reg[6]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[3], mixer_reg[7]);
			waveartist_cmd3(devc, WACMD_SET_MIXER, mixer_reg[8], mixer_reg[9]);

			waveartist_cmd3(devc, WACMD_SET_LEVEL, mixer_reg[10], mixer_reg[11]);
			waveartist_cmd3(devc, WACMD_SET_LEVEL, mixer_reg[12], mixer_reg[13]);
			break;

		case MIXER_PRIVATE3_READ:
			spin_lock_irqsave(&waveartist_lock, flags);

			for (i = 0x30; i < 14 << 8; i += 1 << 8)
				waveartist_cmd(devc, 1, &i, 1, mixer_reg + (i >> 8));

			spin_unlock_irqrestore(&waveartist_lock, flags);

			if (copy_to_user((void *)val, mixer_reg, sizeof(mixer_reg)))
				return -EFAULT;
			break;

		default:
			return -EINVAL;
		}
		return 0;
	}

	/* read back the state from PRIVATE1 */
	case SOUND_MIXER_PRIVATE4:
		val = (devc->spkr_mute_state  ? VNC_MUTE_INTERNAL_SPKR : 0) |
		      (devc->line_mute_state  ? VNC_MUTE_LINE_OUT      : 0) |
		      (devc->handset_detect   ? VNC_HANDSET_DETECT     : 0) |
		      (devc->telephone_detect ? VNC_PHONE_DETECT       : 0) |
		      (devc->no_autoselect    ? VNC_DISABLE_AUTOSWITCH : 0);

		return put_user(val, arg) ? -EFAULT : 0;
	}

	if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
		/*
		 * special case for master volume: if we
		 * received this call - switch from hw
		 * volume control to a software volume
		 * control, till the hw volume is modified
		 * to signal that user wants to be back in
		 * hardware...
		 */
		if ((cmd & 0xff) == SOUND_MIXER_VOLUME)
			devc->use_slider = 0;

		/* speaker output            */
		if ((cmd & 0xff) == SOUND_MIXER_SPEAKER) {
			unsigned int val, l, r;

			if (get_user(val, arg))
				return -EFAULT;

			l = val & 0x7f;
			r = (val & 0x7f00) >> 8;
			val = (l + r) / 2;
			devc->levels[SOUND_MIXER_SPEAKER] = val | (val << 8);
			devc->spkr_mute_state = (val <= 50);
			vnc_mute_spkr(devc);
			return 0;
		}
	}

	return -ENOIOCTLCMD;
}

#endif

static struct address_info cfg;

static int attached;

static int __initdata io = 0;
static int __initdata irq = 0;
static int __initdata dma = 0;
static int __initdata dma2 = 0;


static int __init init_waveartist(void)
{
	const struct waveartist_mixer_info *mix;

	if (!io && machine_is_netwinder()) {
		/*
		 * The NetWinder WaveArtist is at a fixed address.
		 * If the user does not supply an address, use the
		 * well-known parameters.
		 */
		io   = 0x250;
		irq  = 12;
		dma  = 3;
		dma2 = 7;
	}

	mix = &waveartist_mixer;
#ifdef CONFIG_ARCH_NETWINDER
	if (machine_is_netwinder())
		mix = &netwinder_mixer;
#endif

	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma2;

	if (!probe_waveartist(&cfg))
		return -ENODEV;

	attach_waveartist(&cfg, mix);
	attached = 1;

	return 0;
}

static void __exit cleanup_waveartist(void)
{
	if (attached)
		unload_waveartist(&cfg);
}

module_init(init_waveartist);
module_exit(cleanup_waveartist);

#ifndef MODULE
static int __init setup_waveartist(char *str)
{
	/* io, irq, dma, dma2 */
	int ints[5];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma2	= ints[4];

	return 1;
}
__setup("waveartist=", setup_waveartist);
#endif

MODULE_DESCRIPTION("Rockwell WaveArtist RWA-010 sound driver");
module_param(io, int, 0);		/* IO base */
module_param(irq, int, 0);		/* IRQ */
module_param(dma, int, 0);		/* DMA */
module_param(dma2, int, 0);		/* DMA2 */
MODULE_LICENSE("GPL");
