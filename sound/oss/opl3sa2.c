/*
 * sound/opl3sa2.c
 *
 * A low level driver for Yamaha OPL3-SA2 and SA3 cards.
 * NOTE: All traces of the name OPL3-SAx have now (December 2000) been
 *       removed from the driver code, as an email exchange with Yamaha
 *       provided the information that the YMF-719 is indeed just a
 *       re-badged 715.
 *
 * Copyright 1998-2001 Scott Murray <scott@spiteful.org>
 *
 * Originally based on the CS4232 driver (in cs4232.c) by Hannu Savolainen
 * and others.  Now incorporates code/ideas from pss.c, also by Hannu
 * Savolainen.  Both of those files are distributed with the following
 * license:
 *
 * "Copyright (C) by Hannu Savolainen 1993-1997
 *
 *  OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 *  Version 2 (June 1991). See the "COPYING" file distributed with this software
 *  for more info."
 *
 * As such, in accordance with the above license, this file, opl3sa2.c, is
 * distributed under the GNU GENERAL PUBLIC LICENSE (GPL) Version 2 (June 1991).
 * See the "COPYING" file distributed with this software for more information.
 *
 * Change History
 * --------------
 * Scott Murray            Original driver (Jun 14, 1998)
 * Paul J.Y. Lahaie        Changed probing / attach code order
 * Scott Murray            Added mixer support (Dec 03, 1998)
 * Scott Murray            Changed detection code to be more forgiving,
 *                         added force option as last resort,
 *                         fixed ioctl return values. (Dec 30, 1998)
 * Scott Murray            Simpler detection code should work all the time now
 *                         (with thanks to Ben Hutchings for the heuristic),
 *                         removed now unnecessary force option. (Jan 5, 1999)
 * Christoph Hellwig	   Adapted to module_init/module_exit (Mar 4, 2000)
 * Scott Murray            Reworked SA2 versus SA3 mixer code, updated chipset
 *                         version detection code (again!). (Dec 5, 2000)
 * Scott Murray            Adjusted master volume mixer scaling. (Dec 6, 2000)
 * Scott Murray            Based on a patch by Joel Yliluoma (aka Bisqwit),
 *                         integrated wide mixer and adjusted mic, bass, treble
 *                         scaling. (Dec 6, 2000)
 * Scott Murray            Based on a patch by Peter Englmaier, integrated
 *                         ymode and loopback options. (Dec 6, 2000)
 * Scott Murray            Inspired by a patch by Peter Englmaier, and based on
 *                         what ALSA does, added initialization code for the
 *                         default DMA and IRQ settings. (Dec 6, 2000)
 * Scott Murray            Added some more checks to the card detection code,
 *                         based on what ALSA does. (Dec 12, 2000)
 * Scott Murray            Inspired by similar patches from John Fremlin,
 *                         Jim Radford, Mike Rolig, and Ingmar Steen, added 2.4
 *                         ISA PnP API support, mainly based on bits from
 *                         sb_card.c and awe_wave.c. (Dec 12, 2000)
 * Scott Murray            Some small cleanups to the init code output.
 *                         (Jan 7, 2001)
 * Zwane Mwaikambo	   Added PM support. (Dec 4 2001)
 *
 * Adam Belay              Converted driver to new PnP Layer (Oct 12, 2002)
 * Zwane Mwaikambo	   Code, data structure cleanups. (Feb 15 2002)
 * Zwane Mwaikambo	   Free resources during auxiliary device probe
 * 			   failures (Apr 29 2002)
 *   
 */

#include <linux/config.h>
#include <linux/pnp.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include "sound_config.h"

#include "ad1848.h"
#include "mpu401.h"

#define OPL3SA2_MODULE_NAME	"opl3sa2"
#define PFX			OPL3SA2_MODULE_NAME ": "

/* Useful control port indexes: */
#define OPL3SA2_PM	     0x01
#define OPL3SA2_SYS_CTRL     0x02
#define OPL3SA2_IRQ_CONFIG   0x03
#define OPL3SA2_DMA_CONFIG   0x06
#define OPL3SA2_MASTER_LEFT  0x07
#define OPL3SA2_MASTER_RIGHT 0x08
#define OPL3SA2_MIC          0x09
#define OPL3SA2_MISC         0x0A

#define OPL3SA3_WIDE         0x14
#define OPL3SA3_BASS         0x15
#define OPL3SA3_TREBLE       0x16

/* Useful constants: */
#define DEFAULT_VOLUME 50
#define DEFAULT_MIC    50
#define DEFAULT_TIMBRE 0

/* Power saving modes */
#define OPL3SA2_PM_MODE0	0x00
#define OPL3SA2_PM_MODE1	0x04	/* PSV */
#define OPL3SA2_PM_MODE2	0x05	/* PSV | PDX */
#define OPL3SA2_PM_MODE3	0x27	/* ADOWN | PSV | PDN | PDX */


/* For checking against what the card returns: */
#define VERSION_UNKNOWN 0
#define VERSION_YMF711  1
#define VERSION_YMF715  2
#define VERSION_YMF715B 3
#define VERSION_YMF715E 4
/* also assuming that anything > 4 but <= 7 is a 715E */

/* Chipset type constants for use below */
#define CHIPSET_UNKNOWN -1
#define CHIPSET_OPL3SA2 0
#define CHIPSET_OPL3SA3 1
static const char *CHIPSET_TABLE[] = {"OPL3-SA2", "OPL3-SA3"};

#ifdef CONFIG_PNP
#define OPL3SA2_CARDS_MAX 4
#else
#define OPL3SA2_CARDS_MAX 1
#endif

/* This should be pretty obvious */
static int opl3sa2_cards_num;

typedef struct {
	/* device resources */
	unsigned short cfg_port;
	struct address_info cfg;
	struct address_info cfg_mss;
	struct address_info cfg_mpu;
#ifdef CONFIG_PNP
	/* PnP Stuff */
	struct pnp_dev* pdev;
	int activated;			/* Whether said devices have been activated */
#endif
	unsigned int	card;
	int		chipset;	/* What's my version(s)? */
	char		*chipset_name;

	/* mixer data */
	int		mixer;
	unsigned int	volume_l;
	unsigned int	volume_r;
	unsigned int	mic;
	unsigned int	bass_l;
	unsigned int	bass_r;
	unsigned int	treble_l;
	unsigned int	treble_r;
	unsigned int	wide_l;
	unsigned int	wide_r;
} opl3sa2_state_t;
static opl3sa2_state_t opl3sa2_state[OPL3SA2_CARDS_MAX];

	

/* Our parameters */
static int __initdata io	= -1;
static int __initdata mss_io	= -1;
static int __initdata mpu_io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma2	= -1;
static int __initdata ymode	= -1;
static int __initdata loopback	= -1;

#ifdef CONFIG_PNP
/* PnP specific parameters */
static int __initdata isapnp = 1;
static int __initdata multiple = 1;

/* Whether said devices have been activated */
static int opl3sa2_activated[OPL3SA2_CARDS_MAX];
#else
static int __initdata isapnp; /* = 0 */
static int __initdata multiple; /* = 0 */
#endif

MODULE_DESCRIPTION("Module for OPL3-SA2 and SA3 sound cards (uses AD1848 MSS driver).");
MODULE_AUTHOR("Scott Murray <scott@spiteful.org>");
MODULE_LICENSE("GPL");


module_param(io, int, 0);
MODULE_PARM_DESC(io, "Set I/O base of OPL3-SA2 or SA3 card (usually 0x370.  Address must be even and must be from 0x100 to 0xFFE)");

module_param(mss_io, int, 0);
MODULE_PARM_DESC(mss_io, "Set MSS (audio) I/O base (0x530, 0xE80, or other. Address must end in 0 or 4 and must be from 0x530 to 0xF48)");

module_param(mpu_io, int, 0);
MODULE_PARM_DESC(mpu_io, "Set MIDI I/O base (0x330 or other. Address must be even and must be from 0x300 to 0x334)");

module_param(irq, int, 0);
MODULE_PARM_DESC(irq, "Set MSS (audio) IRQ (5, 7, 9, 10, 11, 12)");

module_param(dma, int, 0);
MODULE_PARM_DESC(dma, "Set MSS (audio) first DMA channel (0, 1, 3)");

module_param(dma2, int, 0);
MODULE_PARM_DESC(dma2, "Set MSS (audio) second DMA channel (0, 1, 3)");

module_param(ymode, int, 0);
MODULE_PARM_DESC(ymode, "Set Yamaha 3D enhancement mode (0 = Desktop/Normal, 1 = Notebook PC (1), 2 = Notebook PC (2), 3 = Hi-Fi)");

module_param(loopback, int, 0);
MODULE_PARM_DESC(loopback, "Set A/D input source. Useful for echo cancellation (0 = Mic Rch (default), 1 = Mono output loopback)");

#ifdef CONFIG_PNP
module_param(isapnp, bool, 0);
MODULE_PARM_DESC(isapnp, "When set to 0, ISA PnP support will be disabled");

module_param(multiple, bool, 0);
MODULE_PARM_DESC(multiple, "When set to 0, will not search for multiple cards");
#endif


/*
 * Standard read and write functions
*/

static inline void opl3sa2_write(unsigned short port,
				 unsigned char  index,
				 unsigned char  data)
{
	outb_p(index, port);
	outb(data, port + 1);
}


static inline void opl3sa2_read(unsigned short port,
				unsigned char  index,
				unsigned char* data)
{
	outb_p(index, port);
	*data = inb(port + 1);
}


/*
 * All of the mixer functions...
 */

static void opl3sa2_set_volume(opl3sa2_state_t* devc, int left, int right)
{
	static unsigned char scale[101] = {
		0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0e, 0x0e, 0x0e,
		0x0e, 0x0e, 0x0e, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0c,
		0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x09, 0x09,
		0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08, 0x08,
		0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06,
		0x06, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
		0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03,
		0x03, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00
	};
	unsigned char vol;

	vol = scale[left];

	/* If level is zero, turn on mute */
	if(!left)
		vol |= 0x80;

	opl3sa2_write(devc->cfg_port, OPL3SA2_MASTER_LEFT, vol);

	vol = scale[right];

	/* If level is zero, turn on mute */
	if(!right)
		vol |= 0x80;

	opl3sa2_write(devc->cfg_port, OPL3SA2_MASTER_RIGHT, vol);
}


static void opl3sa2_set_mic(opl3sa2_state_t* devc, int level)
{
	unsigned char vol = 0x1F;

	if((level >= 0) && (level <= 100))
		vol = 0x1F - (unsigned char) (32 * level / 101);

	/* If level is zero, turn on mute */
	if(!level)
		vol |= 0x80;

	opl3sa2_write(devc->cfg_port, OPL3SA2_MIC, vol);
}


static void opl3sa3_set_bass(opl3sa2_state_t* devc, int left, int right)
{
	unsigned char bass;

	bass = left ? ((unsigned char) (8 * left / 101)) : 0; 
	bass |= (right ? ((unsigned char) (8 * right / 101)) : 0) << 4;

	opl3sa2_write(devc->cfg_port, OPL3SA3_BASS, bass);
}


static void opl3sa3_set_treble(opl3sa2_state_t* devc, int left, int right)
{	
	unsigned char treble;

	treble = left ? ((unsigned char) (8 * left / 101)) : 0; 
	treble |= (right ? ((unsigned char) (8 * right / 101)) : 0) << 4;

	opl3sa2_write(devc->cfg_port, OPL3SA3_TREBLE, treble);
}




static void opl3sa2_mixer_reset(opl3sa2_state_t* devc)
{
	if (devc) {
		opl3sa2_set_volume(devc, DEFAULT_VOLUME, DEFAULT_VOLUME);
		devc->volume_l = devc->volume_r = DEFAULT_VOLUME;

		opl3sa2_set_mic(devc, DEFAULT_MIC);
		devc->mic = DEFAULT_MIC;

		if (devc->chipset == CHIPSET_OPL3SA3) {
			opl3sa3_set_bass(devc, DEFAULT_TIMBRE, DEFAULT_TIMBRE);
			devc->bass_l = devc->bass_r = DEFAULT_TIMBRE;
			opl3sa3_set_treble(devc, DEFAULT_TIMBRE, DEFAULT_TIMBRE);
			devc->treble_l = devc->treble_r = DEFAULT_TIMBRE;
		}
	}
}

static inline void arg_to_vol_mono(unsigned int vol, int* value)
{
	int left;
	
	left = vol & 0x00ff;
	if (left > 100)
		left = 100;
	*value = left;
}


static inline void arg_to_vol_stereo(unsigned int vol, int* aleft, int* aright)
{
	arg_to_vol_mono(vol, aleft);
	arg_to_vol_mono(vol >> 8, aright);
}


static inline int ret_vol_mono(int vol)
{
	return ((vol << 8) | vol);
}


static inline int ret_vol_stereo(int left, int right)
{
	return ((right << 8) | left);
}


static int opl3sa2_mixer_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	int retval, value, cmdf = cmd & 0xff;
	int __user *p = (int __user *)arg;

	opl3sa2_state_t* devc = &opl3sa2_state[dev];
	
	switch (cmdf) {
		case SOUND_MIXER_VOLUME:
		case SOUND_MIXER_MIC:
		case SOUND_MIXER_DEVMASK:
		case SOUND_MIXER_STEREODEVS: 
		case SOUND_MIXER_RECMASK:
		case SOUND_MIXER_RECSRC:
		case SOUND_MIXER_CAPS: 
			break;

		default:
			return -EINVAL;
	}
	
	if (((cmd >> 8) & 0xff) != 'M')
		return -EINVAL;
		
	retval = 0;
	if (_SIOC_DIR (cmd) & _SIOC_WRITE) {
		switch (cmdf) {
			case SOUND_MIXER_VOLUME:
				retval = get_user(value, (unsigned __user *) arg);
				if (retval)
					break;
				arg_to_vol_stereo(value, &devc->volume_l, &devc->volume_r);
				opl3sa2_set_volume(devc, devc->volume_l, devc->volume_r);
				value = ret_vol_stereo(devc->volume_l, devc->volume_r);
				retval = put_user(value, p);
				break;
		  
			case SOUND_MIXER_MIC:
				retval = get_user(value, (unsigned __user *) arg);
				if (retval)
					break;
				arg_to_vol_mono(value, &devc->mic);
				opl3sa2_set_mic(devc, devc->mic);
				value = ret_vol_mono(devc->mic);
				retval = put_user(value, p);
				break;

			default:
				retval = -EINVAL;
		}
	}
	else {
		/*
		 * Return parameters
		 */
		switch (cmdf) {
			case SOUND_MIXER_DEVMASK:
				retval = put_user(SOUND_MASK_VOLUME | SOUND_MASK_MIC, p);
				break;
		  
			case SOUND_MIXER_STEREODEVS:
				retval = put_user(SOUND_MASK_VOLUME, p);
				break;
		  
			case SOUND_MIXER_RECMASK:
				/* No recording devices */
				retval = put_user(0, p);
				break;

			case SOUND_MIXER_CAPS:
				retval = put_user(SOUND_CAP_EXCL_INPUT, p);
				break;

			case SOUND_MIXER_RECSRC:
				/* No recording source */
				retval = put_user(0, p);
				break;

			case SOUND_MIXER_VOLUME:
				value = ret_vol_stereo(devc->volume_l, devc->volume_r);
				retval = put_user(value, p);
				break;
			  
			case SOUND_MIXER_MIC:
				value = ret_vol_mono(devc->mic);
				put_user(value, p);
				break;

			default:
				retval = -EINVAL;
		}
	}
	return retval;
}
/* opl3sa2_mixer_ioctl end */


static int opl3sa3_mixer_ioctl(int dev, unsigned int cmd, void __user * arg)
{
	int value, retval, cmdf = cmd & 0xff;

	opl3sa2_state_t* devc = &opl3sa2_state[dev];

	switch (cmdf) {
	case SOUND_MIXER_BASS:
		value = ret_vol_stereo(devc->bass_l, devc->bass_r);
		retval = put_user(value, (int __user *) arg);
		break;
		
	case SOUND_MIXER_TREBLE:
		value = ret_vol_stereo(devc->treble_l, devc->treble_r);
		retval = put_user(value, (int __user *) arg);
		break;

	case SOUND_MIXER_DIGITAL1:
		value = ret_vol_stereo(devc->wide_l, devc->wide_r);
		retval = put_user(value, (int __user *) arg);
		break;

	default:
		retval = -EINVAL;
	}
	return retval;
}
/* opl3sa3_mixer_ioctl end */


static struct mixer_operations opl3sa2_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "OPL3-SA2",
	.name	= "Yamaha OPL3-SA2",
	.ioctl	= opl3sa2_mixer_ioctl
};

static struct mixer_operations opl3sa3_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "OPL3-SA3",
	.name	= "Yamaha OPL3-SA3",
	.ioctl	= opl3sa3_mixer_ioctl
};

/* End of mixer-related stuff */


/*
 * Component probe, attach, unload functions
 */

static inline void __exit unload_opl3sa2_mpu(struct address_info *hw_config)
{
	unload_mpu401(hw_config);
}


static void __init attach_opl3sa2_mss(struct address_info* hw_config, struct resource *ports)
{
	int initial_mixers;

	initial_mixers = num_mixers;
	attach_ms_sound(hw_config, ports, THIS_MODULE);	/* Slot 0 */
	if (hw_config->slots[0] != -1) {
		/* Did the MSS driver install? */
		if(num_mixers == (initial_mixers + 1)) {
			/* The MSS mixer is installed, reroute mixers appropriately */
			AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_CD);
			AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_SYNTH);
			AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_LINE);
		}
		else {
			printk(KERN_ERR PFX "MSS mixer not installed?\n");
		}
	}
}


static inline void __exit unload_opl3sa2_mss(struct address_info* hw_config)
{
	unload_ms_sound(hw_config);
}


static int __init probe_opl3sa2(struct address_info* hw_config, int card)
{
	unsigned char misc;
	unsigned char tmp;
	unsigned char version;

	/*
	 * Try and allocate our I/O port range.
	 */
	if (!request_region(hw_config->io_base, 2, OPL3SA2_MODULE_NAME)) {
		printk(KERN_ERR PFX "Control I/O port %#x not free\n",
		       hw_config->io_base);
		goto out_nodev;
	}

	/*
	 * Check if writing to the read-only version bits of the miscellaneous
	 * register succeeds or not (it should not).
	 */
	opl3sa2_read(hw_config->io_base, OPL3SA2_MISC, &misc);
	opl3sa2_write(hw_config->io_base, OPL3SA2_MISC, misc ^ 0x07);
	opl3sa2_read(hw_config->io_base, OPL3SA2_MISC, &tmp);
	if(tmp != misc) {
		printk(KERN_ERR PFX "Control I/O port %#x is not a YMF7xx chipset!\n",
		       hw_config->io_base);
		goto out_region;
	}

	/*
	 * Check if the MIC register is accessible.
	 */
	opl3sa2_read(hw_config->io_base, OPL3SA2_MIC, &tmp);
	opl3sa2_write(hw_config->io_base, OPL3SA2_MIC, 0x8a);
	opl3sa2_read(hw_config->io_base, OPL3SA2_MIC, &tmp);
	if((tmp & 0x9f) != 0x8a) {
		printk(KERN_ERR
		       PFX "Control I/O port %#x is not a YMF7xx chipset!\n",
		       hw_config->io_base);
		goto out_region;
	}
	opl3sa2_write(hw_config->io_base, OPL3SA2_MIC, tmp);

	/*
	 * Determine chipset type (SA2 or SA3)
	 *
	 * This is done by looking at the chipset version in the lower 3 bits
	 * of the miscellaneous register.
	 */
	version = misc & 0x07;
	printk(KERN_DEBUG PFX "Chipset version = %#x\n", version);
	switch (version) {
		case 0:
			opl3sa2_state[card].chipset = CHIPSET_UNKNOWN;
			printk(KERN_ERR
			       PFX "Unknown Yamaha audio controller version\n");
			break;

		case VERSION_YMF711:
			opl3sa2_state[card].chipset = CHIPSET_OPL3SA2;
			printk(KERN_INFO PFX "Found OPL3-SA2 (YMF711)\n");
			break;

		case VERSION_YMF715:
			opl3sa2_state[card].chipset = CHIPSET_OPL3SA3;
			printk(KERN_INFO
			       PFX "Found OPL3-SA3 (YMF715 or YMF719)\n");
			break;

		case VERSION_YMF715B:
			opl3sa2_state[card].chipset = CHIPSET_OPL3SA3;
			printk(KERN_INFO
			       PFX "Found OPL3-SA3 (YMF715B or YMF719B)\n");
			break;

		case VERSION_YMF715E:
		default:
			opl3sa2_state[card].chipset = CHIPSET_OPL3SA3;
			printk(KERN_INFO
			       PFX "Found OPL3-SA3 (YMF715E or YMF719E)\n");
			break;
	}

	if (opl3sa2_state[card].chipset != CHIPSET_UNKNOWN) {
		/* Generate a pretty name */
		opl3sa2_state[card].chipset_name = (char *)CHIPSET_TABLE[opl3sa2_state[card].chipset];
		return 0;
	}

out_region:
	release_region(hw_config->io_base, 2);
out_nodev:
	return -ENODEV;
}


static void __init attach_opl3sa2(struct address_info* hw_config, int card)
{
	/* Initialize IRQ configuration to IRQ-B: -, IRQ-A: WSS+MPU+OPL3 */
	opl3sa2_write(hw_config->io_base, OPL3SA2_IRQ_CONFIG, 0x0d);

	/* Initialize DMA configuration */
	if(hw_config->dma2 == hw_config->dma) {
		/* Want DMA configuration DMA-B: -, DMA-A: WSS-P+WSS-R */
		opl3sa2_write(hw_config->io_base, OPL3SA2_DMA_CONFIG, 0x03);
	}
	else {
		/* Want DMA configuration DMA-B: WSS-R, DMA-A: WSS-P */
		opl3sa2_write(hw_config->io_base, OPL3SA2_DMA_CONFIG, 0x21);
	}
}


static void __init attach_opl3sa2_mixer(struct address_info *hw_config, int card)
{
	struct mixer_operations* mixer_operations;
	opl3sa2_state_t* devc = &opl3sa2_state[card];

	/* Install master mixer */
	if (devc->chipset == CHIPSET_OPL3SA3) {
		mixer_operations = &opl3sa3_mixer_operations;
	}
	else {
		mixer_operations = &opl3sa2_mixer_operations;
	}

	devc->cfg_port = hw_config->io_base;
	devc->mixer = sound_install_mixer(MIXER_DRIVER_VERSION,
					  mixer_operations->name,
					  mixer_operations,
					  sizeof(struct mixer_operations),
					  devc);
	if(devc->mixer < 0) {
		printk(KERN_ERR PFX "Could not install %s master mixer\n",
			 mixer_operations->name);
	}
	else {
			opl3sa2_mixer_reset(devc);

	}
}


static void opl3sa2_clear_slots(struct address_info* hw_config)
{
	int i;

	for(i = 0; i < 6; i++) {
		hw_config->slots[i] = -1;
	}
}


static void __init opl3sa2_set_ymode(struct address_info* hw_config, int ymode)
{
	/*
	 * Set the Yamaha 3D enhancement mode (aka Ymersion) if asked to and
	 * it's supported.
	 *
	 * 0: Desktop (aka normal)   5-12 cm speakers
	 * 1: Notebook PC mode 1     3 cm speakers
	 * 2: Notebook PC mode 2     1.5 cm speakers
	 * 3: Hi-fi                  16-38 cm speakers
	 */
	if(ymode >= 0 && ymode <= 3) {
		unsigned char sys_ctrl;

		opl3sa2_read(hw_config->io_base, OPL3SA2_SYS_CTRL, &sys_ctrl);
		sys_ctrl = (sys_ctrl & 0xcf) | ((ymode & 3) << 4);
		opl3sa2_write(hw_config->io_base, OPL3SA2_SYS_CTRL, sys_ctrl);
	}
	else {
		printk(KERN_ERR PFX "not setting ymode, it must be one of 0,1,2,3\n");
	}
}


static void __init opl3sa2_set_loopback(struct address_info* hw_config, int loopback)
{
	if(loopback >= 0 && loopback <= 1) {
		unsigned char misc;

		opl3sa2_read(hw_config->io_base, OPL3SA2_MISC, &misc);
		misc = (misc & 0xef) | ((loopback & 1) << 4);
		opl3sa2_write(hw_config->io_base, OPL3SA2_MISC, misc);
	}
	else {
		printk(KERN_ERR PFX "not setting loopback, it must be either 0 or 1\n");
	}
}


static void __exit unload_opl3sa2(struct address_info* hw_config, int card)
{
        /* Release control ports */
	release_region(hw_config->io_base, 2);

	/* Unload mixer */
	if(opl3sa2_state[card].mixer >= 0)
		sound_unload_mixerdev(opl3sa2_state[card].mixer);

}

#ifdef CONFIG_PNP
static struct pnp_device_id pnp_opl3sa2_list[] = {
	{.id = "YMH0021", .driver_data = 0},
	{.id = ""}
};

MODULE_DEVICE_TABLE(pnp, pnp_opl3sa2_list);

static int opl3sa2_pnp_probe(struct pnp_dev *dev, const struct pnp_device_id *dev_id)
{
	int card = opl3sa2_cards_num;

	/* we don't actually want to return an error as the user may have specified
	 * no multiple card search
	 */

	if (opl3sa2_cards_num == OPL3SA2_CARDS_MAX)
		return 0;
	opl3sa2_activated[card] = 1;

	/* Our own config: */
	opl3sa2_state[card].cfg.io_base = pnp_port_start(dev, 4);
	opl3sa2_state[card].cfg.irq     = pnp_irq(dev, 0);
	opl3sa2_state[card].cfg.dma     = pnp_dma(dev, 0);
	opl3sa2_state[card].cfg.dma2    = pnp_dma(dev, 1);

	/* The MSS config: */
	opl3sa2_state[card].cfg_mss.io_base      = pnp_port_start(dev, 1);
	opl3sa2_state[card].cfg_mss.irq          = pnp_irq(dev, 0);
	opl3sa2_state[card].cfg_mss.dma          = pnp_dma(dev, 0);
	opl3sa2_state[card].cfg_mss.dma2         = pnp_dma(dev, 1);
	opl3sa2_state[card].cfg_mss.card_subtype = 1; /* No IRQ or DMA setup */

	opl3sa2_state[card].cfg_mpu.io_base       = pnp_port_start(dev, 3);
	opl3sa2_state[card].cfg_mpu.irq           = pnp_irq(dev, 0);
	opl3sa2_state[card].cfg_mpu.dma           = -1;
	opl3sa2_state[card].cfg_mpu.dma2          = -1;
	opl3sa2_state[card].cfg_mpu.always_detect = 1; /* It's there, so use shared IRQs */

	/* Call me paranoid: */
	opl3sa2_clear_slots(&opl3sa2_state[card].cfg);
	opl3sa2_clear_slots(&opl3sa2_state[card].cfg_mss);
	opl3sa2_clear_slots(&opl3sa2_state[card].cfg_mpu);

	opl3sa2_state[card].pdev = dev;
	opl3sa2_cards_num++;

	return 0;
}

static struct pnp_driver opl3sa2_driver = {
	.name		= "opl3sa2",
	.id_table	= pnp_opl3sa2_list,
	.probe		= opl3sa2_pnp_probe,
};

#endif /* CONFIG_PNP */

/* End of component functions */

/*
 * Install OPL3-SA2 based card(s).
 *
 * Need to have ad1848 and mpu401 loaded ready.
 */
static int __init init_opl3sa2(void)
{
	int card, max;

	/* Sanitize isapnp and multiple settings */
	isapnp = isapnp != 0 ? 1 : 0;
	multiple = multiple != 0 ? 1 : 0;

	max = (multiple && isapnp) ? OPL3SA2_CARDS_MAX : 1;

#ifdef CONFIG_PNP
	if (isapnp){
		pnp_register_driver(&opl3sa2_driver);
		if(!opl3sa2_cards_num){
			printk(KERN_INFO PFX "No PnP cards found\n");
			isapnp = 0;
		}
		max = opl3sa2_cards_num;
	}
#endif

	for (card = 0; card < max; card++) {
		/* If a user wants an I/O then assume they meant it */
		struct resource *ports;
		int base;
		
		if (!isapnp) {
			if (io == -1 || irq == -1 || dma == -1 ||
			    dma2 == -1 || mss_io == -1) {
				printk(KERN_ERR
				       PFX "io, mss_io, irq, dma, and dma2 must be set\n");
				return -EINVAL;
			}
			opl3sa2_cards_num++;

			/*
			 * Our own config:
			 * (NOTE: IRQ and DMA aren't used, so they're set to
			 *  give pretty output from conf_printf. :)
			 */
			opl3sa2_state[card].cfg.io_base = io;
			opl3sa2_state[card].cfg.irq     = irq;
			opl3sa2_state[card].cfg.dma     = dma;
			opl3sa2_state[card].cfg.dma2    = dma2;
	
			/* The MSS config: */
			opl3sa2_state[card].cfg_mss.io_base      = mss_io;
			opl3sa2_state[card].cfg_mss.irq          = irq;
			opl3sa2_state[card].cfg_mss.dma          = dma;
			opl3sa2_state[card].cfg_mss.dma2         = dma2;
			opl3sa2_state[card].cfg_mss.card_subtype = 1; /* No IRQ or DMA setup */

			opl3sa2_state[card].cfg_mpu.io_base       = mpu_io;
			opl3sa2_state[card].cfg_mpu.irq           = irq;
			opl3sa2_state[card].cfg_mpu.dma           = -1;
			opl3sa2_state[card].cfg_mpu.always_detect = 1; /* Use shared IRQs */

			/* Call me paranoid: */
			opl3sa2_clear_slots(&opl3sa2_state[card].cfg);
			opl3sa2_clear_slots(&opl3sa2_state[card].cfg_mss);
			opl3sa2_clear_slots(&opl3sa2_state[card].cfg_mpu);
		}

		/* FIXME: leak */
		if (probe_opl3sa2(&opl3sa2_state[card].cfg, card))
			return -ENODEV;

		base = opl3sa2_state[card].cfg_mss.io_base;

		if (!request_region(base, 4, "WSS config"))
			goto failed;

		ports = request_region(base + 4, 4, "ad1848");
		if (!ports)
			goto failed2;

		if (!probe_ms_sound(&opl3sa2_state[card].cfg_mss, ports)) {
			/*
			 * If one or more cards are already registered, don't
			 * return an error but print a warning.  Note, this
			 * should never really happen unless the hardware or
			 * ISA PnP screwed up.
			 */
			release_region(base + 4, 4);
		failed2:
			release_region(base, 4);
		failed:
			release_region(opl3sa2_state[card].cfg.io_base, 2);

			if (opl3sa2_cards_num) {
				printk(KERN_WARNING
				       PFX "There was a problem probing one "
				       " of the ISA PNP cards, continuing\n");
				opl3sa2_cards_num--;
				continue;
			} else
				return -ENODEV;
		}

		attach_opl3sa2(&opl3sa2_state[card].cfg, card);
		conf_printf(opl3sa2_state[card].chipset_name, &opl3sa2_state[card].cfg);
		attach_opl3sa2_mixer(&opl3sa2_state[card].cfg, card);
		attach_opl3sa2_mss(&opl3sa2_state[card].cfg_mss, ports);

		/* ewww =) */
		opl3sa2_state[card].card = card;

		/*
		 * Set the Yamaha 3D enhancement mode (aka Ymersion) if asked to and
		 * it's supported.
		 */
		if (ymode != -1) {
			if (opl3sa2_state[card].chipset == CHIPSET_OPL3SA2) {
				printk(KERN_ERR
				       PFX "ymode not supported on OPL3-SA2\n");
			}
			else {
				opl3sa2_set_ymode(&opl3sa2_state[card].cfg, ymode);
			}
		}


		/* Set A/D input to Mono loopback if asked to. */
		if (loopback != -1) {
			opl3sa2_set_loopback(&opl3sa2_state[card].cfg, loopback);
		}
		
		/* Attach MPU if we've been asked to do so, failure isn't fatal */
		if (opl3sa2_state[card].cfg_mpu.io_base != -1) {
			int base = opl3sa2_state[card].cfg_mpu.io_base;
			struct resource *ports;
			ports = request_region(base, 2, "mpu401");
			if (!ports)
				goto out;
			if (!probe_mpu401(&opl3sa2_state[card].cfg_mpu, ports)) {
				release_region(base, 2);
				goto out;
			}
			if (attach_mpu401(&opl3sa2_state[card].cfg_mpu, THIS_MODULE)) {
				printk(KERN_ERR PFX "failed to attach MPU401\n");
				opl3sa2_state[card].cfg_mpu.slots[1] = -1;
			}
		}
	}

out:
	if (isapnp) {
		printk(KERN_NOTICE PFX "%d PnP card(s) found.\n", opl3sa2_cards_num);
	}

	return 0;
}


/*
 * Uninstall OPL3-SA2 based card(s).
 */
static void __exit cleanup_opl3sa2(void)
{
	int card;

	for(card = 0; card < opl3sa2_cards_num; card++) {
	        if (opl3sa2_state[card].cfg_mpu.slots[1] != -1) {
			unload_opl3sa2_mpu(&opl3sa2_state[card].cfg_mpu);
 		}
		unload_opl3sa2_mss(&opl3sa2_state[card].cfg_mss);
		unload_opl3sa2(&opl3sa2_state[card].cfg, card);
#ifdef CONFIG_PNP
		pnp_unregister_driver(&opl3sa2_driver);
#endif
	}
}

module_init(init_opl3sa2);
module_exit(cleanup_opl3sa2);

#ifndef MODULE
static int __init setup_opl3sa2(char *str)
{
	/* io, irq, dma, dma2,... */
#ifdef CONFIG_PNP
	int ints[11];
#else
	int ints[9];
#endif
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io       = ints[1];
	irq      = ints[2];
	dma      = ints[3];
	dma2     = ints[4];
	mss_io   = ints[5];
	mpu_io   = ints[6];
	ymode    = ints[7];
	loopback = ints[8];
#ifdef CONFIG_PNP
	isapnp   = ints[9];
	multiple = ints[10];
#endif
	return 1;
}

__setup("opl3sa2=", setup_opl3sa2);
#endif
