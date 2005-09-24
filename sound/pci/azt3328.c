/*
 *  azt3328.c - driver for Aztech AZF3328 based soundcards (e.g. PCI168).
 *  Copyright (C) 2002 by Andreas Mohr <hw7oshyuv3001@sneakemail.com>
 *
 *  Framework borrowed from Bart Hartgers's als4000.c.
 *  Driver developed on PCI168 AP(W) version (PCI rev. 10, subsystem ID 1801),
 *  found in a Fujitsu-Siemens PC ("Cordant", aluminum case).
 *  Other versions are:
 *  PCI168 A(W), sub ID 1800
 *  PCI168 A/AP, sub ID 8000
 *  Please give me feedback in case you try my driver with one of these!!
 *
 * GPL LICENSE
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * NOTES
 *  Since Aztech does not provide any chipset documentation,
 *  even on repeated request to various addresses,
 *  and the answer that was finally given was negative
 *  (and I was stupid enough to manage to get hold of a PCI168 soundcard
 *  in the first place >:-P}),
 *  I was forced to base this driver on reverse engineering
 *  (3 weeks' worth of evenings filled with driver work).
 *  (and no, I did NOT go the easy way: to pick up a PCI128 for 9 Euros)
 *
 *  The AZF3328 chip (note: AZF3328, *not* AZT3328, that's just the driver name
 *  for compatibility reasons) has the following features:
 *
 *  - builtin AC97 conformant codec (SNR over 80dB)
 *    (really AC97 compliant?? I really doubt it when looking
 *    at the mixer register layout)
 *  - builtin genuine OPL3
 *  - full duplex 16bit playback/record at independent sampling rate
 *  - MPU401 (+ legacy address support) FIXME: how to enable legacy addr??
 *  - game port (legacy address support)
 *  - built-in General DirectX timer having a 20 bits counter
 *    with 1us resolution (FIXME: where is it?)
 *  - I2S serial port for external DAC
 *  - supports 33MHz PCI spec 2.1, PCI power management 1.0, compliant with ACPI
 *  - supports hardware volume control
 *  - single chip low cost solution (128 pin QFP)
 *  - supports programmable Sub-vendor and Sub-system ID
 *    required for Microsoft's logo compliance (FIXME: where?)
 *  - PCI168 AP(W) card: power amplifier with 4 Watts/channel at 4 Ohms
 *
 *  Certain PCI versions of this card are susceptible to DMA traffic underruns
 *  in some systems (resulting in sound crackling/clicking/popping),
 *  probably because they don't have a DMA FIFO buffer or so.
 *  Overview (PCI ID/PCI subID/PCI rev.):
 *  - no DMA crackling on SiS735: 0x50DC/0x1801/16
 *  - unknown performance: 0x50DC/0x1801/10
 *  
 *  Crackling happens with VIA chipsets or, in my case, an SiS735, which is
 *  supposed to be very fast and supposed to get rid of crackling much
 *  better than a VIA, yet ironically I still get crackling, like many other
 *  people with the same chipset.
 *  Possible remedies:
 *  - plug card into a different PCI slot, preferrably one that isn't shared
 *    too much (this helps a lot, but not completely!)
 *  - get rid of PCI VGA card, use AGP instead
 *  - upgrade or downgrade BIOS
 *  - fiddle with PCI latency settings (setpci -v -s BUSID latency_timer=XX)
 *    Not too helpful.
 *  - Disable ACPI/power management/"Auto Detect RAM/PCI Clk" in BIOS
 * 
 * BUGS
 *  - when Ctrl-C'ing mpg321, the playback loops a bit
 *    (premature DMA playback reset?)
 *  - full-duplex sometimes breaks (IRQ management issues?).
 *    Once even a spontaneous REBOOT happened!!!
 * 
 * TODO
 *  - test MPU401 MIDI playback etc.
 *  - power management (CONFIG_PM). See e.g. intel8x0 or cs4281.
 *    This would be nice since the chip runs a bit hot, and it's *required*
 *    anyway for proper ACPI power management. In other words: rest
 *    assured that I *will* implement this very soon; as soon as Linux 2.5.x
 *    has power management that's bugfree enough to work properly on my desktop.
 *  - figure out what all unknown port bits are responsible for
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/initval.h>
#include "azt3328.h"

MODULE_AUTHOR("Andreas Mohr <hw7oshyuv3001@sneakemail.com>");
MODULE_DESCRIPTION("Aztech AZF3328 (PCI168)");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Aztech,AZF3328}}");

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

#define DEBUG_MISC	0
#define DEBUG_CALLS	0
#define DEBUG_MIXER	0
#define DEBUG_PLAY_REC	0
#define DEBUG_IO	0
#define MIXER_TESTING	0

#if DEBUG_MISC
#define snd_azf3328_dbgmisc(format, args...) printk(KERN_ERR format, ##args)
#else
#define snd_azf3328_dbgmisc(format, args...)
#endif		

#if DEBUG_CALLS
#define snd_azf3328_dbgcalls(format, args...) printk(format, ##args)
#define snd_azf3328_dbgcallenter() printk(KERN_ERR "entering %s\n", __FUNCTION__)
#define snd_azf3328_dbgcallleave() printk(KERN_ERR "leaving %s\n", __FUNCTION__)
#else
#define snd_azf3328_dbgcalls(format, args...)
#define snd_azf3328_dbgcallenter()
#define snd_azf3328_dbgcallleave()
#endif		

#if DEBUG_MIXER
#define snd_azf3328_dbgmixer(format, args...) printk(format, ##args)
#else
#define snd_azf3328_dbgmixer(format, args...)
#endif		

#if DEBUG_PLAY_REC
#define snd_azf3328_dbgplay(format, args...) printk(KERN_ERR format, ##args)
#else
#define snd_azf3328_dbgplay(format, args...)
#endif		

#if DEBUG_IO
#define snd_azf3328_dbgio(chip, where) \
	    printk(KERN_ERR "%s: IDX_IO_PLAY_FLAGS %04x, IDX_IO_PLAY_IRQMASK %04x, IDX_IO_IRQSTATUS %04x\n", where, inw(chip->codec_port+IDX_IO_PLAY_FLAGS), inw(chip->codec_port+IDX_IO_PLAY_IRQMASK), inw(chip->codec_port+IDX_IO_IRQSTATUS))
#else
#define snd_azf3328_dbgio(chip, where)
#endif
	    
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for AZF3328 soundcard.");

static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for AZF3328 soundcard.");

static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable AZF3328 soundcard.");

#ifdef SUPPORT_JOYSTICK
static int joystick[SNDRV_CARDS];
module_param_array(joystick, bool, NULL, 0444);
MODULE_PARM_DESC(joystick, "Enable joystick for AZF3328 soundcard.");
#endif

typedef struct _snd_azf3328 azf3328_t;

struct _snd_azf3328 {
	int irq;

	unsigned long codec_port;
	unsigned long io2_port;
	unsigned long mpu_port;
	unsigned long synth_port;
	unsigned long mixer_port;

#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif

	struct pci_dev *pci;
	snd_card_t *card;

	snd_pcm_t *pcm;
	snd_rawmidi_t *rmidi;
	snd_pcm_substream_t *playback_substream;
	snd_pcm_substream_t *capture_substream;
	unsigned int is_playing;
	unsigned int is_recording;

	spinlock_t reg_lock;
};

static struct pci_device_id snd_azf3328_ids[] = {
	{ 0x122D, 0x50DC, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },   /* PCI168/3328 */
	{ 0x122D, 0x80DA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },   /* 3328 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_azf3328_ids);

static inline void snd_azf3328_io2_write(azf3328_t *chip, int reg, unsigned char value)
{
	outb(value, chip->io2_port + reg);
}

static inline unsigned char snd_azf3328_io2_read(azf3328_t *chip, int reg)
{
	return inb(chip->io2_port + reg);
}

static void snd_azf3328_mixer_write(azf3328_t *chip, int reg, unsigned long value, int type)
{
	switch(type) {
	case WORD_VALUE:
		outw(value, chip->mixer_port + reg);
		break;
	case DWORD_VALUE:
		outl(value, chip->mixer_port + reg);
		break;
	case BYTE_VALUE:
		outb(value, chip->mixer_port + reg);
		break;
	}
}

static void snd_azf3328_mixer_set_mute(azf3328_t *chip, int reg, int do_mute)
{
	unsigned char oldval;

	/* the mute bit is on the *second* (i.e. right) register of a
	 * left/right channel setting */
	oldval = inb(chip->mixer_port + reg + 1);
	if (do_mute)
		oldval |= 0x80;
	else
		oldval &= ~0x80;
	outb(oldval, chip->mixer_port + reg + 1);
}

static void snd_azf3328_mixer_write_volume_gradually(azf3328_t *chip, int reg, unsigned char dst_vol_left, unsigned char dst_vol_right, int chan_sel, int delay)
{
	unsigned char curr_vol_left = 0, curr_vol_right = 0;
	int left_done = 0, right_done = 0;
	
	snd_azf3328_dbgcallenter();
	if (chan_sel & SET_CHAN_LEFT)
		curr_vol_left  = inb(chip->mixer_port + reg + 1);
	else
		left_done = 1;
	if (chan_sel & SET_CHAN_RIGHT)
		curr_vol_right = inb(chip->mixer_port + reg + 0);
	else
		right_done = 1;
	
	/* take care of muting flag (0x80) contained in left channel */
	if (curr_vol_left & 0x80)
		dst_vol_left |= 0x80;
	else
		dst_vol_left &= ~0x80;

	do
	{
		if (!left_done)
		{
			if (curr_vol_left > dst_vol_left)
				curr_vol_left--;
			else
			if (curr_vol_left < dst_vol_left)
				curr_vol_left++;
			else
			    left_done = 1;
			outb(curr_vol_left, chip->mixer_port + reg + 1);
		}
		if (!right_done)
		{
			if (curr_vol_right > dst_vol_right)
				curr_vol_right--;
			else
			if (curr_vol_right < dst_vol_right)
				curr_vol_right++;
			else
			    right_done = 1;
			/* during volume change, the right channel is crackling
			 * somewhat more than the left channel, unfortunately.
			 * This seems to be a hardware issue. */
			outb(curr_vol_right, chip->mixer_port + reg + 0);
		}
		if (delay)
			mdelay(delay);
	}
	while ((!left_done) || (!right_done));
	snd_azf3328_dbgcallleave();
}

/*
 * general mixer element
 */
typedef struct azf3328_mixer_reg {
	unsigned int reg;
	unsigned int lchan_shift, rchan_shift;
	unsigned int mask;
	unsigned int invert: 1;
	unsigned int stereo: 1;
	unsigned int enum_c: 4;
} azf3328_mixer_reg_t;

#define COMPOSE_MIXER_REG(reg,lchan_shift,rchan_shift,mask,invert,stereo,enum_c) \
 ((reg) | (lchan_shift << 8) | (rchan_shift << 12) | (mask << 16) | (invert << 24) | (stereo << 25) | (enum_c << 26))

static void snd_azf3328_mixer_reg_decode(azf3328_mixer_reg_t *r, unsigned long val)
{
	r->reg = val & 0xff;
	r->lchan_shift = (val >> 8) & 0x0f;
	r->rchan_shift = (val >> 12) & 0x0f;
	r->mask = (val >> 16) & 0xff;
	r->invert = (val >> 24) & 1;
	r->stereo = (val >> 25) & 1;
	r->enum_c = (val >> 26) & 0x0f;
}

/*
 * mixer switches/volumes
 */

#define AZF3328_MIXER_SWITCH(xname, reg, shift, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_azf3328_info_mixer, \
  .get = snd_azf3328_get_mixer, .put = snd_azf3328_put_mixer, \
  .private_value = COMPOSE_MIXER_REG(reg, shift, 0, 0x1, invert, 0, 0), \
}

#define AZF3328_MIXER_VOL_STEREO(xname, reg, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_azf3328_info_mixer, \
  .get = snd_azf3328_get_mixer, .put = snd_azf3328_put_mixer, \
  .private_value = COMPOSE_MIXER_REG(reg, 8, 0, mask, invert, 1, 0), \
}

#define AZF3328_MIXER_VOL_MONO(xname, reg, mask, is_right_chan) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_azf3328_info_mixer, \
  .get = snd_azf3328_get_mixer, .put = snd_azf3328_put_mixer, \
  .private_value = COMPOSE_MIXER_REG(reg, is_right_chan ? 0 : 8, 0, mask, 1, 0, 0), \
}

#define AZF3328_MIXER_VOL_SPECIAL(xname, reg, mask, shift, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_azf3328_info_mixer, \
  .get = snd_azf3328_get_mixer, .put = snd_azf3328_put_mixer, \
  .private_value = COMPOSE_MIXER_REG(reg, shift, 0, mask, invert, 0, 0), \
}

#define AZF3328_MIXER_ENUM(xname, reg, enum_c, shift) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_azf3328_info_mixer_enum, \
  .get = snd_azf3328_get_mixer_enum, .put = snd_azf3328_put_mixer_enum, \
  .private_value = COMPOSE_MIXER_REG(reg, shift, 0, 0, 0, 0, enum_c), \
}

static int snd_azf3328_info_mixer(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	azf3328_mixer_reg_t reg;

	snd_azf3328_dbgcallenter();
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	uinfo->type = reg.mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = reg.stereo + 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = reg.mask;
	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_get_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	azf3328_t *chip = snd_kcontrol_chip(kcontrol);
	azf3328_mixer_reg_t reg;
	unsigned int oreg, val;

	snd_azf3328_dbgcallenter();
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);

	oreg = inw(chip->mixer_port + reg.reg);
	val = (oreg >> reg.lchan_shift) & reg.mask;
	if (reg.invert)
		val = reg.mask - val;
	ucontrol->value.integer.value[0] = val;
	if (reg.stereo) {
		val = (oreg >> reg.rchan_shift) & reg.mask;
		if (reg.invert)
			val = reg.mask - val;
		ucontrol->value.integer.value[1] = val;
	}
	snd_azf3328_dbgmixer("get: %02x is %04x -> vol %02lx|%02lx (shift %02d|%02d, mask %02x, inv. %d, stereo %d)\n", reg.reg, oreg, ucontrol->value.integer.value[0], ucontrol->value.integer.value[1], reg.lchan_shift, reg.rchan_shift, reg.mask, reg.invert, reg.stereo);
	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_put_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	azf3328_t *chip = snd_kcontrol_chip(kcontrol);
	azf3328_mixer_reg_t reg;
	unsigned int oreg, nreg, val;

	snd_azf3328_dbgcallenter();
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	oreg = inw(chip->mixer_port + reg.reg);
	val = ucontrol->value.integer.value[0] & reg.mask;
	if (reg.invert)
		val = reg.mask - val;
	nreg = oreg & ~(reg.mask << reg.lchan_shift);
	nreg |= (val << reg.lchan_shift);
	if (reg.stereo) {
		val = ucontrol->value.integer.value[1] & reg.mask;
		if (reg.invert)
			val = reg.mask - val;
		nreg &= ~(reg.mask << reg.rchan_shift);
		nreg |= (val << reg.rchan_shift);
	}
	if (reg.mask >= 0x07) /* it's a volume control, so better take care */
		snd_azf3328_mixer_write_volume_gradually(chip, reg.reg, nreg >> 8, nreg & 0xff, SET_CHAN_LEFT|SET_CHAN_RIGHT, 0); /* just set both channels, doesn't matter */
	else
        	outw(nreg, chip->mixer_port + reg.reg);

	snd_azf3328_dbgmixer("put: %02x to %02lx|%02lx, oreg %04x; shift %02d|%02d -> nreg %04x; after: %04x\n", reg.reg, ucontrol->value.integer.value[0], ucontrol->value.integer.value[1], oreg, reg.lchan_shift, reg.rchan_shift, nreg, inw(chip->mixer_port + reg.reg));
	snd_azf3328_dbgcallleave();
	return (nreg != oreg);
}

static int snd_azf3328_info_mixer_enum(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	azf3328_mixer_reg_t reg;
	static char *texts1[2] = { "ModemOut1", "ModemOut2" };
	static char *texts2[2] = { "MonoSelectSource1", "MonoSelectSource2" };
        static char *texts3[8] = {
                "Mic", "CD", "Video", "Aux", "Line",
                "Mix", "Mix Mono", "Phone"
        };

	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
        uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
        uinfo->count = (reg.reg == IDX_MIXER_REC_SELECT) ? 2 : 1;
        uinfo->value.enumerated.items = reg.enum_c;
        if (uinfo->value.enumerated.item > reg.enum_c - 1U)
                uinfo->value.enumerated.item = reg.enum_c - 1U;
	if (reg.reg == IDX_MIXER_ADVCTL2)
	{
		if (reg.lchan_shift == 8) /* modem out sel */
			strcpy(uinfo->value.enumerated.name, texts1[uinfo->value.enumerated.item]);
		else /* mono sel source */
			strcpy(uinfo->value.enumerated.name, texts2[uinfo->value.enumerated.item]);
	}
	else
        	strcpy(uinfo->value.enumerated.name, texts3[uinfo->value.enumerated.item]
);
        return 0;
}

static int snd_azf3328_get_mixer_enum(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	azf3328_mixer_reg_t reg;
        azf3328_t *chip = snd_kcontrol_chip(kcontrol);
        unsigned short val;
        
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	val = inw(chip->mixer_port + reg.reg);
	if (reg.reg == IDX_MIXER_REC_SELECT)
	{
        	ucontrol->value.enumerated.item[0] = (val >> 8) & (reg.enum_c - 1);
        	ucontrol->value.enumerated.item[1] = (val >> 0) & (reg.enum_c - 1);
	}
	else
        	ucontrol->value.enumerated.item[0] = (val >> reg.lchan_shift) & (reg.enum_c - 1);
	snd_azf3328_dbgmixer("get_enum: %02x is %04x -> %d|%d (shift %02d, enum_c %d)\n", reg.reg, val, ucontrol->value.enumerated.item[0], ucontrol->value.enumerated.item[1], reg.lchan_shift, reg.enum_c);
        return 0;
}

static int snd_azf3328_put_mixer_enum(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	azf3328_mixer_reg_t reg;
        azf3328_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int oreg, nreg, val;
        
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	oreg = inw(chip->mixer_port + reg.reg);
	val = oreg;
	if (reg.reg == IDX_MIXER_REC_SELECT)
	{
        	if (ucontrol->value.enumerated.item[0] > reg.enum_c - 1U ||
            	ucontrol->value.enumerated.item[1] > reg.enum_c - 1U)
                	return -EINVAL;
        	val = (ucontrol->value.enumerated.item[0] << 8) |
        	      (ucontrol->value.enumerated.item[1] << 0);
	}
	else
	{
        	if (ucontrol->value.enumerated.item[0] > reg.enum_c - 1U)
                	return -EINVAL;
		val &= ~((reg.enum_c - 1) << reg.lchan_shift);
        	val |= (ucontrol->value.enumerated.item[0] << reg.lchan_shift);
	}
	outw(val, chip->mixer_port + reg.reg);
	nreg = val;

	snd_azf3328_dbgmixer("put_enum: %02x to %04x, oreg %04x\n", reg.reg, val, oreg);
	return (nreg != oreg);
}

static snd_kcontrol_new_t snd_azf3328_mixer_controls[] __devinitdata = {
	AZF3328_MIXER_SWITCH("Master Playback Switch", IDX_MIXER_PLAY_MASTER, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Master Playback Volume", IDX_MIXER_PLAY_MASTER, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Wave Playback Switch", IDX_MIXER_WAVEOUT, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Wave Playback Volume", IDX_MIXER_WAVEOUT, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Wave Playback 3D Bypass", IDX_MIXER_ADVCTL2, 7, 1),
	AZF3328_MIXER_SWITCH("FM Playback Switch", IDX_MIXER_FMSYNTH, 15, 1),
	AZF3328_MIXER_VOL_STEREO("FM Playback Volume", IDX_MIXER_FMSYNTH, 0x1f, 1),
	AZF3328_MIXER_SWITCH("CD Playback Switch", IDX_MIXER_CDAUDIO, 15, 1),
	AZF3328_MIXER_VOL_STEREO("CD Playback Volume", IDX_MIXER_CDAUDIO, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Capture Switch", IDX_MIXER_REC_VOLUME, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Capture Volume", IDX_MIXER_REC_VOLUME, 0x0f, 0),
	AZF3328_MIXER_ENUM("Capture Source", IDX_MIXER_REC_SELECT, 8, 0),
	AZF3328_MIXER_SWITCH("Mic Playback Switch", IDX_MIXER_MIC, 15, 1),
	AZF3328_MIXER_VOL_MONO("Mic Playback Volume", IDX_MIXER_MIC, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Mic Boost (+20dB)", IDX_MIXER_MIC, 6, 0),
	AZF3328_MIXER_SWITCH("Line Playback Switch", IDX_MIXER_LINEIN, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Line Playback Volume", IDX_MIXER_LINEIN, 0x1f, 1),
	AZF3328_MIXER_SWITCH("PCBeep Playback Switch", IDX_MIXER_PCBEEP, 15, 1),
	AZF3328_MIXER_VOL_SPECIAL("PCBeep Playback Volume", IDX_MIXER_PCBEEP, 0x0f, 1, 1),
	AZF3328_MIXER_SWITCH("Video Playback Switch", IDX_MIXER_VIDEO, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Video Playback Volume", IDX_MIXER_VIDEO, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Aux Playback Switch", IDX_MIXER_AUX, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Aux Playback Volume", IDX_MIXER_AUX, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Modem Playback Switch", IDX_MIXER_MODEMOUT, 15, 1),
	AZF3328_MIXER_VOL_MONO("Modem Playback Volume", IDX_MIXER_MODEMOUT, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Modem Capture Switch", IDX_MIXER_MODEMIN, 15, 1),
	AZF3328_MIXER_VOL_MONO("Modem Capture Volume", IDX_MIXER_MODEMIN, 0x1f, 1),
	AZF3328_MIXER_ENUM("Modem Out Select", IDX_MIXER_ADVCTL2, 2, 8),
	AZF3328_MIXER_ENUM("Mono Select Source", IDX_MIXER_ADVCTL2, 2, 9),
	AZF3328_MIXER_VOL_SPECIAL("Tone Control - Treble", IDX_MIXER_BASSTREBLE, 0x07, 1, 0),
	AZF3328_MIXER_VOL_SPECIAL("Tone Control - Bass", IDX_MIXER_BASSTREBLE, 0x07, 9, 0),
	AZF3328_MIXER_SWITCH("3D Control - Toggle", IDX_MIXER_ADVCTL2, 13, 0),
	AZF3328_MIXER_VOL_SPECIAL("3D Control - Volume", IDX_MIXER_ADVCTL1, 0x07, 1, 0), /* "3D Width" */
	AZF3328_MIXER_VOL_SPECIAL("3D Control - Space", IDX_MIXER_ADVCTL1, 0x03, 8, 0), /* "Hifi 3D" */
#if MIXER_TESTING
	AZF3328_MIXER_SWITCH("0", IDX_MIXER_ADVCTL2, 0, 0),
	AZF3328_MIXER_SWITCH("1", IDX_MIXER_ADVCTL2, 1, 0),
	AZF3328_MIXER_SWITCH("2", IDX_MIXER_ADVCTL2, 2, 0),
	AZF3328_MIXER_SWITCH("3", IDX_MIXER_ADVCTL2, 3, 0),
	AZF3328_MIXER_SWITCH("4", IDX_MIXER_ADVCTL2, 4, 0),
	AZF3328_MIXER_SWITCH("5", IDX_MIXER_ADVCTL2, 5, 0),
	AZF3328_MIXER_SWITCH("6", IDX_MIXER_ADVCTL2, 6, 0),
	AZF3328_MIXER_SWITCH("7", IDX_MIXER_ADVCTL2, 7, 0),
	AZF3328_MIXER_SWITCH("8", IDX_MIXER_ADVCTL2, 8, 0),
	AZF3328_MIXER_SWITCH("9", IDX_MIXER_ADVCTL2, 9, 0),
	AZF3328_MIXER_SWITCH("10", IDX_MIXER_ADVCTL2, 10, 0),
	AZF3328_MIXER_SWITCH("11", IDX_MIXER_ADVCTL2, 11, 0),
	AZF3328_MIXER_SWITCH("12", IDX_MIXER_ADVCTL2, 12, 0),
	AZF3328_MIXER_SWITCH("13", IDX_MIXER_ADVCTL2, 13, 0),
	AZF3328_MIXER_SWITCH("14", IDX_MIXER_ADVCTL2, 14, 0),
	AZF3328_MIXER_SWITCH("15", IDX_MIXER_ADVCTL2, 15, 0),
#endif
};

#define AZF3328_INIT_VALUES (sizeof(snd_azf3328_init_values)/sizeof(unsigned int)/2)

static unsigned int snd_azf3328_init_values[][2] = {
        { IDX_MIXER_PLAY_MASTER,	MIXER_MUTE_MASK|0x1f1f },
        { IDX_MIXER_MODEMOUT,		MIXER_MUTE_MASK|0x1f1f },
	{ IDX_MIXER_BASSTREBLE,		0x0000 },
	{ IDX_MIXER_PCBEEP,		MIXER_MUTE_MASK|0x1f1f },
	{ IDX_MIXER_MODEMIN,		MIXER_MUTE_MASK|0x1f1f },
	{ IDX_MIXER_MIC,		MIXER_MUTE_MASK|0x001f },
	{ IDX_MIXER_LINEIN,		MIXER_MUTE_MASK|0x1f1f },
	{ IDX_MIXER_CDAUDIO,		MIXER_MUTE_MASK|0x1f1f },
	{ IDX_MIXER_VIDEO,		MIXER_MUTE_MASK|0x1f1f },
	{ IDX_MIXER_AUX,		MIXER_MUTE_MASK|0x1f1f },
        { IDX_MIXER_WAVEOUT,		MIXER_MUTE_MASK|0x1f1f },
        { IDX_MIXER_FMSYNTH,		MIXER_MUTE_MASK|0x1f1f },
        { IDX_MIXER_REC_VOLUME,		MIXER_MUTE_MASK|0x0707 },
};

static int __devinit snd_azf3328_mixer_new(azf3328_t *chip)
{
	snd_card_t *card;
	snd_kcontrol_new_t *sw;
	unsigned int idx;
	int err;

	snd_azf3328_dbgcallenter();
	snd_assert(chip != NULL && chip->card != NULL, return -EINVAL);

	card = chip->card;

	/* mixer reset */
	snd_azf3328_mixer_write(chip, IDX_MIXER_RESET, 0x0, WORD_VALUE);

	/* mute and zero volume channels */
	for (idx = 0; idx < AZF3328_INIT_VALUES; idx++) {
		snd_azf3328_mixer_write(chip, snd_azf3328_init_values[idx][0], snd_azf3328_init_values[idx][1], WORD_VALUE);
	}
	
	/* add mixer controls */
	sw = snd_azf3328_mixer_controls;
	for (idx = 0; idx < ARRAY_SIZE(snd_azf3328_mixer_controls); idx++, sw++) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(sw, chip))) < 0)
			return err;
	}
	snd_component_add(card, "AZF3328 mixer");
	strcpy(card->mixername, "AZF3328 mixer");

	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	int res;
	snd_azf3328_dbgcallenter();
	res = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	snd_azf3328_dbgcallleave();
	return res;
}

static int snd_azf3328_hw_free(snd_pcm_substream_t * substream)
{
	snd_azf3328_dbgcallenter();
	snd_pcm_lib_free_pages(substream);
	snd_azf3328_dbgcallleave();
	return 0;
}

static void snd_azf3328_setfmt(azf3328_t *chip,
			       unsigned int reg,
			       unsigned int bitrate,
			       unsigned int format_width,
			       unsigned int channels
)
{
	unsigned int val = 0xff00;
	unsigned long flags;

	snd_azf3328_dbgcallenter();
	switch (bitrate) {
	case  5512: val |= 0x0d; break; /* the AZF3328 names it "5510" for some strange reason */
	case  6620: val |= 0x0b; break;
	case  8000: val |= 0x00; break;
	case  9600: val |= 0x08; break;
	case 11025: val |= 0x01; break;
	case 16000: val |= 0x02; break;
	case 22050: val |= 0x03; break;
	case 32000: val |= 0x04; break;
	case 44100: val |= 0x05; break;
	case 48000: val |= 0x06; break;
	case 64000: val |= 0x07; break;
	default:
		snd_printk("unknown bitrate %d, assuming 44.1kHz!\n", bitrate);
		val |= 0x05; /* 44100 */
		break;
	}
	/* val = 0xff07; 3m27.993s (65301Hz; -> 64000Hz???) */
	/* val = 0xff09; 17m15.098s (13123,478Hz; -> 12000Hz???) */
	/* val = 0xff0a; 47m30.599s (4764,891Hz; -> 4800Hz???) */
	/* val = 0xff0c; 57m0.510s (4010,263Hz; -> 4000Hz???) */
	/* val = 0xff05; 5m11.556s (... -> 44100Hz) */
	/* val = 0xff03; 10m21.529s (21872,463Hz; -> 22050Hz???) */
	/* val = 0xff0f; 20m41.883s (10937,993Hz; -> 11025Hz???) */
	/* val = 0xff0d; 41m23.135s (5523,600Hz; -> 5512Hz???) */
	/* val = 0xff0e; 28m30.777s (8017Hz; -> 8000Hz???) */
	if (channels == 2)
		val |= SOUNDFORMAT_FLAG_2CHANNELS;

	if (format_width == 16)
		val |= SOUNDFORMAT_FLAG_16BIT;

	spin_lock_irqsave(&chip->reg_lock, flags);
	
	/* set bitrate/format */
	outw(val, chip->codec_port+reg);
	
	/* changing the bitrate/format settings switches off the
	 * audio output with an annoying click in case of 8/16bit format change
	 * (maybe shutting down DAC/ADC?), thus immediately
	 * do some tweaking to reenable it and get rid of the clicking
	 * (FIXME: yes, it works, but what exactly am I doing here?? :)
	 * FIXME: does this have some side effects for full-duplex
	 * or other dramatic side effects? */
	if (reg == IDX_IO_PLAY_SOUNDFORMAT) /* only do it for playback */
		outw(inw(chip->codec_port + IDX_IO_PLAY_FLAGS)|DMA_PLAY_SOMETHING1|DMA_PLAY_SOMETHING2|SOMETHING_ALMOST_ALWAYS_SET|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE, chip->codec_port + IDX_IO_PLAY_FLAGS);

	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_azf3328_dbgcallleave();
}

static void snd_azf3328_setdmaa(azf3328_t *chip,
				long unsigned int addr,
                                unsigned int count,
                                unsigned int size,
				int do_recording)
{
	long unsigned int addr1;
	long unsigned int addr2;
	unsigned int count1;
	unsigned int count2;
	unsigned long flags;
	int reg_offs = do_recording ? 0x20 : 0x00;

	snd_azf3328_dbgcallenter();
	/* AZF3328 uses a two buffer pointer DMA playback approach */
	if (!chip->is_playing)
	{
		addr1 = addr;
		addr2 = addr+(size/2);
		count1 = (size/2)-1;
		count2 = (size/2)-1;
#if DEBUG_PLAY_REC
		snd_azf3328_dbgplay("setting dma: buf1 %08lx[%d], buf2 %08lx[%d]\n", addr1, count1, addr2, count2);
#endif
		spin_lock_irqsave(&chip->reg_lock, flags);
		outl(addr1, chip->codec_port+reg_offs+IDX_IO_PLAY_DMA_START_1);
		outl(addr2, chip->codec_port+reg_offs+IDX_IO_PLAY_DMA_START_2);
		outw(count1, chip->codec_port+reg_offs+IDX_IO_PLAY_DMA_LEN_1);
		outw(count2, chip->codec_port+reg_offs+IDX_IO_PLAY_DMA_LEN_2);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
	snd_azf3328_dbgcallleave();
}

static int snd_azf3328_playback_prepare(snd_pcm_substream_t *substream)
{
#if 0
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
        unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);
#endif

	snd_azf3328_dbgcallenter();
#if 0
	snd_azf3328_setfmt(chip, IDX_IO_PLAY_SOUNDFORMAT, runtime->rate, snd_pcm_format_width(runtime->format), runtime->channels);
	snd_azf3328_setdmaa(chip, runtime->dma_addr, count, size, 0);
#endif
	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_capture_prepare(snd_pcm_substream_t * substream)
{
#if 0
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
        unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);
#endif

	snd_azf3328_dbgcallenter();
#if 0
	snd_azf3328_setfmt(chip, IDX_IO_REC_SOUNDFORMAT, runtime->rate, snd_pcm_format_width(runtime->format), runtime->channels);
	snd_azf3328_setdmaa(chip, runtime->dma_addr, count, size, 1);
#endif
	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_playback_trigger(snd_pcm_substream_t * substream, int cmd)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int result = 0;
	unsigned int status1;

	snd_azf3328_dbgcalls("snd_azf3328_playback_trigger cmd %d\n", cmd);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:

		snd_azf3328_dbgio(chip, "trigger1");

		/* mute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 1);

		snd_azf3328_setfmt(chip, IDX_IO_PLAY_SOUNDFORMAT, runtime->rate, snd_pcm_format_width(runtime->format), runtime->channels);

		spin_lock(&chip->reg_lock);
		/* stop playback */
		status1 = inw(chip->codec_port+IDX_IO_PLAY_FLAGS);
		status1 &= ~DMA_RESUME;
		outw(status1, chip->codec_port+IDX_IO_PLAY_FLAGS);
	    
		/* FIXME: clear interrupts or what??? */
		outw(0xffff, chip->codec_port+IDX_IO_PLAY_IRQMASK);
		spin_unlock(&chip->reg_lock);

		snd_azf3328_setdmaa(chip, runtime->dma_addr, snd_pcm_lib_period_bytes(substream), snd_pcm_lib_buffer_bytes(substream), 0);

		spin_lock(&chip->reg_lock);
#ifdef WIN9X
		/* FIXME: enable playback/recording??? */
		status1 |= DMA_PLAY_SOMETHING1 | DMA_PLAY_SOMETHING2;
		outw(status1, chip->codec_port+IDX_IO_PLAY_FLAGS);

		/* start playback again */
		/* FIXME: what is this value (0x0010)??? */
		status1 |= DMA_RESUME | DMA_EPILOGUE_SOMETHING;
		outw(status1, chip->codec_port+IDX_IO_PLAY_FLAGS);
#else /* NT4 */
		outw(0x00, chip->codec_port+IDX_IO_PLAY_FLAGS);
		outw(DMA_PLAY_SOMETHING1, chip->codec_port+IDX_IO_PLAY_FLAGS);
		outw(DMA_PLAY_SOMETHING1|DMA_PLAY_SOMETHING2, chip->codec_port+IDX_IO_PLAY_FLAGS);
		outw(DMA_RESUME|SOMETHING_ALMOST_ALWAYS_SET|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE, chip->codec_port+IDX_IO_PLAY_FLAGS);
#endif
		spin_unlock(&chip->reg_lock);

		/* now unmute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 0);

		snd_azf3328_dbgio(chip, "trigger2");
		chip->is_playing = 1;
		break;
        case SNDRV_PCM_TRIGGER_STOP:
		/* mute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 1);

		spin_lock(&chip->reg_lock);
		/* stop playback */
		status1 = inw(chip->codec_port+IDX_IO_PLAY_FLAGS);

		status1 &= ~DMA_RESUME;
		outw(status1, chip->codec_port+IDX_IO_PLAY_FLAGS);

		status1 |= DMA_PLAY_SOMETHING1;
		outw(status1, chip->codec_port+IDX_IO_PLAY_FLAGS);

		status1 &= ~DMA_PLAY_SOMETHING1;
		outw(status1, chip->codec_port+IDX_IO_PLAY_FLAGS);
		spin_unlock(&chip->reg_lock);
	    
		/* now unmute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 0);
		chip->is_playing = 0;
		break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_printk("FIXME: SNDRV_PCM_TRIGGER_PAUSE_PUSH NIY!\n");
                break;
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_printk("FIXME: SNDRV_PCM_TRIGGER_PAUSE_RELEASE NIY!\n");
                break;
        default:
                return -EINVAL;
	}
	
	snd_azf3328_dbgcallleave();
	return result;
}

/* this is just analogous to playback; I'm not quite sure whether recording
 * should actually be triggered like that */
static int snd_azf3328_capture_trigger(snd_pcm_substream_t * substream, int cmd)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int result = 0;
	unsigned int status1;

	snd_azf3328_dbgcalls("snd_azf3328_capture_trigger cmd %d\n", cmd);
        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:

		snd_azf3328_dbgio(chip, "trigger1");

		snd_azf3328_setfmt(chip, IDX_IO_REC_SOUNDFORMAT, runtime->rate, snd_pcm_format_width(runtime->format), runtime->channels);

		spin_lock(&chip->reg_lock);
		/* stop recording */
		status1 = inw(chip->codec_port+IDX_IO_REC_FLAGS);
		status1 &= ~DMA_RESUME;
		outw(status1, chip->codec_port+IDX_IO_REC_FLAGS);
	    
		/* FIXME: clear interrupts or what??? */
		outw(0xffff, chip->codec_port+IDX_IO_REC_IRQMASK);
		spin_unlock(&chip->reg_lock);

		snd_azf3328_setdmaa(chip, runtime->dma_addr, snd_pcm_lib_period_bytes(substream), snd_pcm_lib_buffer_bytes(substream), 1);

		spin_lock(&chip->reg_lock);
#ifdef WIN9X
		/* FIXME: enable playback/recording??? */
		status1 |= DMA_PLAY_SOMETHING1 | DMA_PLAY_SOMETHING2;
		outw(status1, chip->codec_port+IDX_IO_REC_FLAGS);

		/* start playback again */
		/* FIXME: what is this value (0x0010)??? */
		status1 |= DMA_RESUME | DMA_EPILOGUE_SOMETHING;
		outw(status1, chip->codec_port+IDX_IO_REC_FLAGS);
#else
		outw(0x00, chip->codec_port+IDX_IO_REC_FLAGS);
		outw(DMA_PLAY_SOMETHING1, chip->codec_port+IDX_IO_REC_FLAGS);
		outw(DMA_PLAY_SOMETHING1|DMA_PLAY_SOMETHING2, chip->codec_port+IDX_IO_REC_FLAGS);
		outw(DMA_RESUME|SOMETHING_ALMOST_ALWAYS_SET|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE, chip->codec_port+IDX_IO_REC_FLAGS);
#endif
		spin_unlock(&chip->reg_lock);

		snd_azf3328_dbgio(chip, "trigger2");
		chip->is_playing = 1;
		break;
        case SNDRV_PCM_TRIGGER_STOP:
		spin_lock(&chip->reg_lock);
		/* stop recording */
		status1 = inw(chip->codec_port+IDX_IO_REC_FLAGS);

		status1 &= ~DMA_RESUME;
		outw(status1, chip->codec_port+IDX_IO_REC_FLAGS);

		status1 |= DMA_PLAY_SOMETHING1;
		outw(status1, chip->codec_port+IDX_IO_REC_FLAGS);

		status1 &= ~DMA_PLAY_SOMETHING1;
		outw(status1, chip->codec_port+IDX_IO_REC_FLAGS);
		spin_unlock(&chip->reg_lock);
	    
		chip->is_playing = 0;
		break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_printk("FIXME: SNDRV_PCM_TRIGGER_PAUSE_PUSH NIY!\n");
                break;
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_printk("FIXME: SNDRV_PCM_TRIGGER_PAUSE_RELEASE NIY!\n");
                break;
        default:
                return -EINVAL;
	}
	
	snd_azf3328_dbgcallleave();
	return result;
}

static snd_pcm_uframes_t snd_azf3328_playback_pointer(snd_pcm_substream_t * substream)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	unsigned long bufptr, playptr;
	unsigned long result;
	snd_pcm_uframes_t frmres;

#ifdef QUERY_HARDWARE
	bufptr = inl(chip->codec_port+IDX_IO_PLAY_DMA_START_1);
#else
	bufptr = substream->runtime->dma_addr;
#endif
	playptr = inl(chip->codec_port+IDX_IO_PLAY_DMA_CURRPOS);

	result = playptr - bufptr;
	frmres = bytes_to_frames( substream->runtime, result );
	snd_azf3328_dbgplay("result %lx, playptr %lx (base %x), frames %ld\n", result, playptr, substream->runtime->dma_addr, frmres);
	return frmres;
}

static snd_pcm_uframes_t snd_azf3328_capture_pointer(snd_pcm_substream_t * substream)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	unsigned long bufptr, recptr;
	unsigned long result;
	snd_pcm_uframes_t frmres;

#ifdef QUERY_HARDWARE
	bufptr = inl(chip->codec_port+IDX_IO_REC_DMA_START_1);
#else
	bufptr = substream->runtime->dma_addr;
#endif
	recptr = inl(chip->codec_port+IDX_IO_REC_DMA_CURRPOS);

	result = recptr - bufptr;
	frmres = bytes_to_frames( substream->runtime, result );
	snd_azf3328_dbgplay("result %lx, rec ptr %lx (base %x), frames %ld\n", result, recptr, substream->runtime->dma_addr, frmres);
	return frmres;
}

static irqreturn_t snd_azf3328_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	azf3328_t *chip = dev_id;
	unsigned int status, which;
	static unsigned long count;

	status  = inw(chip->codec_port+IDX_IO_IRQSTATUS);

        /* fast path out, to ease interrupt sharing */
	if (!(status & (IRQ_PLAYBACK|IRQ_RECORDING|IRQ_MPU401|IRQ_SOMEIRQ)))
		return IRQ_NONE; /* must be interrupt for another device */

	snd_azf3328_dbgplay("Interrupt %ld!\nIDX_IO_PLAY_FLAGS %04x, IDX_IO_PLAY_IRQMASK %04x, IDX_IO_IRQSTATUS %04x\n", count, inw(chip->codec_port+IDX_IO_PLAY_FLAGS), inw(chip->codec_port+IDX_IO_PLAY_IRQMASK), inw(chip->codec_port+IDX_IO_IRQSTATUS));
		
	if (status & IRQ_PLAYBACK)
	{
		spin_lock(&chip->reg_lock);
		which = inw(chip->codec_port+IDX_IO_PLAY_IRQMASK);
		if (which & IRQ_FINISHED_PLAYBUF_1)
			/* ack IRQ */
			outw(which | IRQ_FINISHED_PLAYBUF_1, chip->codec_port+IDX_IO_PLAY_IRQMASK);
		if (which & IRQ_FINISHED_PLAYBUF_2)
			/* ack IRQ */
			outw(which | IRQ_FINISHED_PLAYBUF_2, chip->codec_port+IDX_IO_PLAY_IRQMASK);
		if (which & IRQ_PLAY_SOMETHING)
		{
			snd_azf3328_dbgplay("azt3328: unknown play IRQ type occurred, please report!\n");
		}
		if (chip->pcm && chip->playback_substream)
		{
			snd_azf3328_dbgplay("which %x, playptr %lx\n", which, inl(chip->codec_port+IDX_IO_PLAY_DMA_CURRPOS));
			snd_pcm_period_elapsed(chip->playback_substream);
			snd_azf3328_dbgplay("period done, playptr %lx.\n", inl(chip->codec_port+IDX_IO_PLAY_DMA_CURRPOS));
		}
		else
			snd_azf3328_dbgplay("azt3328: ouch, irq handler problem!\n");
               	spin_unlock(&chip->reg_lock);
	}
	if (status & IRQ_RECORDING)
	{
                spin_lock(&chip->reg_lock);
		which = inw(chip->codec_port+IDX_IO_REC_IRQMASK);
		if (which & IRQ_FINISHED_RECBUF_1)
			/* ack interrupt */
			outw(which | IRQ_FINISHED_RECBUF_1, chip->codec_port+IDX_IO_REC_IRQMASK);
		if (which & IRQ_FINISHED_RECBUF_2)
			/* ack interrupt */
			outw(which | IRQ_FINISHED_RECBUF_2, chip->codec_port+IDX_IO_REC_IRQMASK);
		if (which & IRQ_REC_SOMETHING)
		{
			snd_azf3328_dbgplay("azt3328: unknown rec IRQ type occurred, please report!\n");
		}
		if (chip->pcm && chip->capture_substream)
		{
			snd_azf3328_dbgplay("which %x, recptr %lx\n", which, inl(chip->codec_port+IDX_IO_REC_DMA_CURRPOS));
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(chip->capture_substream);
			spin_lock(&chip->reg_lock);
			snd_azf3328_dbgplay("period done, recptr %lx.\n", inl(chip->codec_port+IDX_IO_REC_DMA_CURRPOS));
		}
               	spin_unlock(&chip->reg_lock);
	}
	if (status & IRQ_MPU401)
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);
	if (status & IRQ_SOMEIRQ)
		snd_azf3328_dbgplay("azt3328: unknown IRQ type occurred, please report!\n");
	count++;
	return IRQ_HANDLED;
}

/*****************************************************************/

static snd_pcm_hardware_t snd_azf3328_playback =
{
	/* FIXME!! Correct? */
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
				SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE,
	.rates =		SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_KNOT,
	.rate_min =		5512,
	.rate_max =		64000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	/* FIXME: maybe that card actually has a FIFO?
	 * Hmm, it seems newer revisions do have one, but we still don't know
	 * its size... */
	.fifo_size =		0,
};

static snd_pcm_hardware_t snd_azf3328_capture =
{
	/* FIXME */
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
				SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE,
	.rates =		SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_KNOT,
	.rate_min =		5512,
	.rate_max =		64000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};


static unsigned int snd_azf3328_fixed_rates[] = {
	5512, 6620, 8000, 9600, 11025, 16000, 22050, 32000, 44100, 48000, 64000
};
static snd_pcm_hw_constraint_list_t snd_azf3328_hw_constraints_rates = {
	.count = ARRAY_SIZE(snd_azf3328_fixed_rates), 
	.list = snd_azf3328_fixed_rates,
	.mask = 0,
};

/*****************************************************************/

static int snd_azf3328_playback_open(snd_pcm_substream_t * substream)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_azf3328_dbgcallenter();
	chip->playback_substream = substream;
	runtime->hw = snd_azf3328_playback;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &snd_azf3328_hw_constraints_rates);
	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_capture_open(snd_pcm_substream_t * substream)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_azf3328_dbgcallenter();
	chip->capture_substream = substream;
	runtime->hw = snd_azf3328_capture;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &snd_azf3328_hw_constraints_rates);
	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_playback_close(snd_pcm_substream_t * substream)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);

	snd_azf3328_dbgcallenter();

	chip->playback_substream = NULL;
	snd_azf3328_dbgcallleave();
	return 0;
}

static int snd_azf3328_capture_close(snd_pcm_substream_t * substream)
{
	azf3328_t *chip = snd_pcm_substream_chip(substream);

	snd_azf3328_dbgcallenter();
	chip->capture_substream = NULL;
	snd_azf3328_dbgcallleave();
	return 0;
}

/******************************************************************/

static snd_pcm_ops_t snd_azf3328_playback_ops = {
	.open =		snd_azf3328_playback_open,
	.close =	snd_azf3328_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_azf3328_hw_params,
	.hw_free =	snd_azf3328_hw_free,
	.prepare =	snd_azf3328_playback_prepare,
	.trigger =	snd_azf3328_playback_trigger,
	.pointer =	snd_azf3328_playback_pointer
};

static snd_pcm_ops_t snd_azf3328_capture_ops = {
	.open =		snd_azf3328_capture_open,
	.close =	snd_azf3328_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_azf3328_hw_params,
	.hw_free =	snd_azf3328_hw_free,
	.prepare =	snd_azf3328_capture_prepare,
	.trigger =	snd_azf3328_capture_trigger,
	.pointer =	snd_azf3328_capture_pointer
};

static void snd_azf3328_pcm_free(snd_pcm_t *pcm)
{
	azf3328_t *chip = pcm->private_data;
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_azf3328_pcm(azf3328_t *chip, int device)
{
	snd_pcm_t *pcm;
	int err;

	snd_azf3328_dbgcallenter();
	if ((err = snd_pcm_new(chip->card, "AZF3328 DSP", device, 1, 1, &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_azf3328_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_azf3328_capture_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_azf3328_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 64*1024);

	snd_azf3328_dbgcallleave();
	return 0;
}

/******************************************************************/

#ifdef SUPPORT_JOYSTICK
static int __devinit snd_azf3328_config_joystick(azf3328_t *chip, int dev)
{
	struct gameport *gp;
	struct resource *r;

	if (!joystick[dev])
		return -ENODEV;

	if (!(r = request_region(0x200, 8, "AZF3328 gameport"))) {
		printk(KERN_WARNING "azt3328: cannot reserve joystick ports\n");
		return -EBUSY;
	}

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "azt3328: cannot allocate memory for gameport\n");
		release_resource(r);
		kfree_nocheck(r);
		return -ENOMEM;
	}

	gameport_set_name(gp, "AZF3328 Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gp->io = 0x200;
	gameport_set_port_data(gp, r);

	snd_azf3328_io2_write(chip, IDX_IO2_LEGACY_ADDR,
			      snd_azf3328_io2_read(chip, IDX_IO2_LEGACY_ADDR) | LEGACY_JOY);

	gameport_register_port(chip->gameport);

	return 0;
}

static void snd_azf3328_free_joystick(azf3328_t *chip)
{
	if (chip->gameport) {
		struct resource *r = gameport_get_port_data(chip->gameport);

		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
		/* disable gameport */
		snd_azf3328_io2_write(chip, IDX_IO2_LEGACY_ADDR,
				      snd_azf3328_io2_read(chip, IDX_IO2_LEGACY_ADDR) & ~LEGACY_JOY);
		release_resource(r);
		kfree_nocheck(r);
	}
}
#else
static inline int snd_azf3328_config_joystick(azf3328_t *chip, int dev) { return -ENOSYS; }
static inline void snd_azf3328_free_joystick(azf3328_t *chip) { }
#endif

/******************************************************************/

static int snd_azf3328_free(azf3328_t *chip)
{
        if (chip->irq < 0)
                goto __end_hw;

	/* reset (close) mixer */
	snd_azf3328_mixer_set_mute(chip, IDX_MIXER_PLAY_MASTER, 1); /* first mute master volume */
	snd_azf3328_mixer_write(chip, IDX_MIXER_RESET, 0x0, WORD_VALUE);

        /* interrupt setup - mask everything */
	/* FIXME */

        synchronize_irq(chip->irq);
      __end_hw:
	snd_azf3328_free_joystick(chip);
        if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);

        kfree(chip);
        return 0;
}

static int snd_azf3328_dev_free(snd_device_t *device)
{
	azf3328_t *chip = device->device_data;
	return snd_azf3328_free(chip);
}

#if 0
/* check whether a bit can be modified */
static void snd_azf3328_test_bit(unsigned int reg, int bit)
{
	unsigned char val, valoff, valon;

	val = inb(reg);

	outb(val & ~(1 << bit), reg);
	valoff = inb(reg);

	outb(val|(1 << bit), reg);
	valon = inb(reg);
	
	outb(val, reg);

	printk(KERN_ERR "reg %04x bit %d: %02x %02x %02x\n", reg, bit, val, valoff, valon);
}
#endif

static int __devinit snd_azf3328_create(snd_card_t * card,
                                         struct pci_dev *pci,
                                         unsigned long device_type,
                                         azf3328_t ** rchip)
{
	azf3328_t *chip;
	int err;
	static snd_device_ops_t ops = {
		.dev_free =     snd_azf3328_dev_free,
	};
	u16 tmp;

	*rchip = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	spin_lock_init(&chip->reg_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	/* check if we can restrict PCI DMA transfers to 24 bits */
	if (pci_set_dma_mask(pci, 0x00ffffff) < 0 ||
	    pci_set_consistent_dma_mask(pci, 0x00ffffff) < 0) {
		snd_printk("architecture does not support 24bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	if ((err = pci_request_regions(pci, "Aztech AZF3328")) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}

	chip->codec_port = pci_resource_start(pci, 0);
	chip->io2_port = pci_resource_start(pci, 1);
	chip->mpu_port = pci_resource_start(pci, 2);
	chip->synth_port = pci_resource_start(pci, 3);
	chip->mixer_port = pci_resource_start(pci, 4);

	if (request_irq(pci->irq, snd_azf3328_interrupt, SA_INTERRUPT|SA_SHIRQ, card->shortname, (void *)chip)) {
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		snd_azf3328_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	pci_set_master(pci);
	synchronize_irq(chip->irq);

	snd_azf3328_dbgmisc("codec_port 0x%lx, io2_port 0x%lx, mpu_port 0x%lx, synth_port 0x%lx, mixer_port 0x%lx, irq %d\n", chip->codec_port, chip->io2_port, chip->mpu_port, chip->synth_port, chip->mixer_port, chip->irq);

	snd_azf3328_dbgmisc("io2 %02x %02x %02x %02x %02x %02x\n", snd_azf3328_io2_read(chip, 0), snd_azf3328_io2_read(chip, 1), snd_azf3328_io2_read(chip, 2), snd_azf3328_io2_read(chip, 3), snd_azf3328_io2_read(chip, 4), snd_azf3328_io2_read(chip, 5));

	for (tmp=0; tmp <= 0x01; tmp += 1)
		snd_azf3328_dbgmisc("0x%02x: opl 0x%04x, mpu300 0x%04x, mpu310 0x%04x, mpu320 0x%04x, mpu330 0x%04x\n", tmp, inb(0x388 + tmp), inb(0x300 + tmp), inb(0x310 + tmp), inb(0x320 + tmp), inb(0x330 + tmp));

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_azf3328_free(chip);
		return err;
	}

	/* create mixer interface & switches */
	if ((err = snd_azf3328_mixer_new(chip)) < 0)
		return err;

#if 0
	/* set very low bitrate to reduce noise and power consumption? */
	snd_azf3328_setfmt(chip, IDX_IO_PLAY_SOUNDFORMAT, 5512, 8, 1);
#endif

	/* standard chip init stuff */
	spin_lock_irq(&chip->reg_lock);
	outb(DMA_PLAY_SOMETHING2|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE, chip->codec_port + IDX_IO_PLAY_FLAGS);
	outb(DMA_PLAY_SOMETHING2|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE, chip->codec_port + IDX_IO_SOMETHING_FLAGS);
	outb(DMA_PLAY_SOMETHING2|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE, chip->codec_port + IDX_IO_REC_FLAGS);
	outb(0x0, chip->codec_port + IDX_IO_IRQ63H);

	spin_unlock_irq(&chip->reg_lock);

	snd_card_set_dev(card, &pci->dev);

	*rchip = chip;
	return 0;
}

static int __devinit snd_azf3328_probe(struct pci_dev *pci,
					  const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	azf3328_t *chip;
	opl3_t *opl3;
	int err;

	snd_azf3328_dbgcallenter();
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0 );
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "AZF3328");
	strcpy(card->shortname, "Aztech AZF3328 (PCI168)");

        if ((err = snd_azf3328_create(card, pci, pci_id->driver_data, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_mpu401_uart_new( card, 0, MPU401_HW_MPU401,
				        chip->mpu_port, 1, pci->irq, 0,
				        &chip->rmidi)) < 0) {
		snd_printk("azf3328: no MPU-401 device at 0x%lx?\n", chip->mpu_port);
		snd_card_free(card);
		return err;
	}

	if ((err = snd_azf3328_pcm(chip, 0)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (snd_opl3_create(card, chip->synth_port, chip->synth_port+2,
			    OPL3_HW_AUTO, 1, &opl3) < 0) {
		snd_printk("azf3328: no OPL3 device at 0x%lx-0x%lx?\n",
			   chip->synth_port, chip->synth_port+2 );
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	snd_azf3328_dbgio(chip, "create");

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->codec_port, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

#ifdef MODULE
	printk(
"azt3328: Experimental driver for Aztech AZF3328-based soundcards such as PCI168.\n"
"azt3328: ZERO support from Aztech: you might think hard about future purchase.\n"
"azt3328: Feel free to contact hw7oshyuv3001@sneakemail.com for bug reports etc.!\n");
#endif

	if (snd_azf3328_config_joystick(chip, dev) < 0)
		snd_azf3328_io2_write(chip, IDX_IO2_LEGACY_ADDR,
			      snd_azf3328_io2_read(chip, IDX_IO2_LEGACY_ADDR) & ~LEGACY_JOY);

	pci_set_drvdata(pci, card);
	dev++;

	snd_azf3328_dbgcallleave();
	return 0;
}

static void __devexit snd_azf3328_remove(struct pci_dev *pci)
{
	snd_azf3328_dbgcallenter();
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
	snd_azf3328_dbgcallleave();
}

static struct pci_driver driver = {
	.name = "AZF3328",
	.owner = THIS_MODULE,
	.id_table = snd_azf3328_ids,
	.probe = snd_azf3328_probe,
	.remove = __devexit_p(snd_azf3328_remove),
};

static int __init alsa_card_azf3328_init(void)
{
	int err;
	snd_azf3328_dbgcallenter();
	err = pci_register_driver(&driver);
	snd_azf3328_dbgcallleave();
	return err;
}

static void __exit alsa_card_azf3328_exit(void)
{
	snd_azf3328_dbgcallenter();
	pci_unregister_driver(&driver);
	snd_azf3328_dbgcallleave();
}

module_init(alsa_card_azf3328_init)
module_exit(alsa_card_azf3328_exit)
