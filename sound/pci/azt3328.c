/*
 *  azt3328.c - driver for Aztech AZF3328 based soundcards (e.g. PCI168).
 *  Copyright (C) 2002, 2005 by Andreas Mohr <andi AT lisas.de>
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
 *  (and no, I did NOT go the easy way: to pick up a SB PCI128 for 9 Euros)
 *
 *  The AZF3328 chip (note: AZF3328, *not* AZT3328, that's just the driver name
 *  for compatibility reasons) has the following features:
 *
 *  - builtin AC97 conformant codec (SNR over 80dB)
 *    Note that "conformant" != "compliant"!! this chip's mixer register layout
 *    *differs* from the standard AC97 layout:
 *    they chose to not implement the headphone register (which is not a
 *    problem since it's merely optional), yet when doing this, they committed
 *    the grave sin of letting other registers follow immediately instead of
 *    keeping a headphone dummy register, thereby shifting the mixer register
 *    addresses illegally. So far unfortunately it looks like the very flexible
 *    ALSA AC97 support is still not enough to easily compensate for such a
 *    grave layout violation despite all tweaks and quirks mechanisms it offers.
 *  - builtin genuine OPL3
 *  - full duplex 16bit playback/record at independent sampling rate
 *  - MPU401 (+ legacy address support) FIXME: how to enable legacy addr??
 *  - game port (legacy address support)
 *  - built-in General DirectX timer having a 20 bits counter
 *    with 1us resolution (see below!)
 *  - I2S serial port for external DAC
 *  - supports 33MHz PCI spec 2.1, PCI power management 1.0, compliant with ACPI
 *  - supports hardware volume control
 *  - single chip low cost solution (128 pin QFP)
 *  - supports programmable Sub-vendor and Sub-system ID
 *    required for Microsoft's logo compliance (FIXME: where?)
 *  - PCI168 AP(W) card: power amplifier with 4 Watts/channel at 4 Ohms
 *
 *  Note that this driver now is actually *better* than the Windows driver,
 *  since it additionally supports the card's 1MHz DirectX timer - just try
 *  the following snd-seq module parameters etc.:
 *  - options snd-seq seq_default_timer_class=2 seq_default_timer_sclass=0
 *    seq_default_timer_card=0 seq_client_load=1 seq_default_timer_device=0
 *    seq_default_timer_subdevice=0 seq_default_timer_resolution=1000000
 *  - "timidity -iAv -B2,8 -Os -EFreverb=0"
 *  - "pmidi -p 128:0 jazz.mid"
 *
 *  Certain PCI versions of this card are susceptible to DMA traffic underruns
 *  in some systems (resulting in sound crackling/clicking/popping),
 *  probably because they don't have a DMA FIFO buffer or so.
 *  Overview (PCI ID/PCI subID/PCI rev.):
 *  - no DMA crackling on SiS735: 0x50DC/0x1801/16
 *  - unknown performance: 0x50DC/0x1801/10
 *    (well, it's not bad on an Athlon 1800 with now very optimized IRQ handler)
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
 *  - full-duplex might *still* be problematic, not fully tested recently
 * 
 * TODO
 *  - test MPU401 MIDI playback etc.
 *  - add some power micro-management (disable various units of the card
 *    as long as they're unused). However this requires I/O ports which I
 *    haven't figured out yet and which thus might not even exist...
 *    The standard suspend/resume functionality could probably make use of
 *    some improvement, too...
 *  - figure out what all unknown port bits are responsible for
 *  - figure out some cleverly evil scheme to possibly make ALSA AC97 code
 *    fully accept our quite incompatible ""AC97"" mixer and thus save some
 *    code (but I'm not too optimistic that doing this is possible at all)
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/initval.h>
#include "azt3328.h"

MODULE_AUTHOR("Andreas Mohr <andi AT lisas.de>");
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
#define DEBUG_TIMER	0
#define MIXER_TESTING	0

#if DEBUG_MISC
#define snd_azf3328_dbgmisc(format, args...) printk(KERN_ERR format, ##args)
#else
#define snd_azf3328_dbgmisc(format, args...)
#endif		

#if DEBUG_CALLS
#define snd_azf3328_dbgcalls(format, args...) printk(format, ##args)
#define snd_azf3328_dbgcallenter() printk(KERN_ERR "--> %s\n", __FUNCTION__)
#define snd_azf3328_dbgcallleave() printk(KERN_ERR "<-- %s\n", __FUNCTION__)
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

#if DEBUG_MISC
#define snd_azf3328_dbgtimer(format, args...) printk(KERN_ERR format, ##args)
#else
#define snd_azf3328_dbgtimer(format, args...)
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

static int seqtimer_scaling = 128;
module_param(seqtimer_scaling, int, 0444);
MODULE_PARM_DESC(seqtimer_scaling, "Set 1024000Hz sequencer timer scale factor (lockup danger!). Default 128.");

struct snd_azf3328 {
	/* often-used fields towards beginning, then grouped */
	unsigned long codec_port;
	unsigned long io2_port;
	unsigned long mpu_port;
	unsigned long synth_port;
	unsigned long mixer_port;

	spinlock_t reg_lock;

	struct snd_timer *timer;
	
	struct snd_pcm *pcm;
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;
	unsigned int is_playing;
	unsigned int is_recording;

	struct snd_card *card;
	struct snd_rawmidi *rmidi;

#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif

	struct pci_dev *pci;
	int irq;

#ifdef CONFIG_PM
	/* register value containers for power management
	 * Note: not always full I/O range preserved (just like Win driver!) */
	u16 saved_regs_codec [AZF_IO_SIZE_CODEC_PM / 2];
	u16 saved_regs_io2   [AZF_IO_SIZE_IO2_PM / 2];
	u16 saved_regs_mpu   [AZF_IO_SIZE_MPU_PM / 2];
	u16 saved_regs_synth[AZF_IO_SIZE_SYNTH_PM / 2];
	u16 saved_regs_mixer[AZF_IO_SIZE_MIXER_PM / 2];
#endif
};

static const struct pci_device_id snd_azf3328_ids[] = {
	{ 0x122D, 0x50DC, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },   /* PCI168/3328 */
	{ 0x122D, 0x80DA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },   /* 3328 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_azf3328_ids);

static inline void
snd_azf3328_codec_outb(const struct snd_azf3328 *chip, int reg, u8 value)
{
	outb(value, chip->codec_port + reg);
}

static inline u8
snd_azf3328_codec_inb(const struct snd_azf3328 *chip, int reg)
{
	return inb(chip->codec_port + reg);
}

static inline void
snd_azf3328_codec_outw(const struct snd_azf3328 *chip, int reg, u16 value)
{
	outw(value, chip->codec_port + reg);
}

static inline u16
snd_azf3328_codec_inw(const struct snd_azf3328 *chip, int reg)
{
	return inw(chip->codec_port + reg);
}

static inline void
snd_azf3328_codec_outl(const struct snd_azf3328 *chip, int reg, u32 value)
{
	outl(value, chip->codec_port + reg);
}

static inline void
snd_azf3328_io2_outb(const struct snd_azf3328 *chip, int reg, u8 value)
{
	outb(value, chip->io2_port + reg);
}

static inline u8
snd_azf3328_io2_inb(const struct snd_azf3328 *chip, int reg)
{
	return inb(chip->io2_port + reg);
}

static inline void
snd_azf3328_mixer_outw(const struct snd_azf3328 *chip, int reg, u16 value)
{
	outw(value, chip->mixer_port + reg);
}

static inline u16
snd_azf3328_mixer_inw(const struct snd_azf3328 *chip, int reg)
{
	return inw(chip->mixer_port + reg);
}

static void
snd_azf3328_mixer_set_mute(const struct snd_azf3328 *chip, int reg, int do_mute)
{
	unsigned long portbase = chip->mixer_port + reg + 1;
	unsigned char oldval;

	/* the mute bit is on the *second* (i.e. right) register of a
	 * left/right channel setting */
	oldval = inb(portbase);
	if (do_mute)
		oldval |= 0x80;
	else
		oldval &= ~0x80;
	outb(oldval, portbase);
}

static void
snd_azf3328_mixer_write_volume_gradually(const struct snd_azf3328 *chip, int reg, unsigned char dst_vol_left, unsigned char dst_vol_right, int chan_sel, int delay)
{
	unsigned long portbase = chip->mixer_port + reg;
	unsigned char curr_vol_left = 0, curr_vol_right = 0;
	int left_done = 0, right_done = 0;
	
	snd_azf3328_dbgcallenter();
	if (chan_sel & SET_CHAN_LEFT)
		curr_vol_left  = inb(portbase + 1);
	else
		left_done = 1;
	if (chan_sel & SET_CHAN_RIGHT)
		curr_vol_right = inb(portbase + 0);
	else
		right_done = 1;
	
	/* take care of muting flag (0x80) contained in left channel */
	if (curr_vol_left & 0x80)
		dst_vol_left |= 0x80;
	else
		dst_vol_left &= ~0x80;

	do {
		if (!left_done) {
			if (curr_vol_left > dst_vol_left)
				curr_vol_left--;
			else
			if (curr_vol_left < dst_vol_left)
				curr_vol_left++;
			else
			    left_done = 1;
			outb(curr_vol_left, portbase + 1);
		}
		if (!right_done) {
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
			outb(curr_vol_right, portbase + 0);
		}
		if (delay)
			mdelay(delay);
	} while ((!left_done) || (!right_done));
	snd_azf3328_dbgcallleave();
}

/*
 * general mixer element
 */
struct azf3328_mixer_reg {
	unsigned int reg;
	unsigned int lchan_shift, rchan_shift;
	unsigned int mask;
	unsigned int invert: 1;
	unsigned int stereo: 1;
	unsigned int enum_c: 4;
};

#define COMPOSE_MIXER_REG(reg,lchan_shift,rchan_shift,mask,invert,stereo,enum_c) \
 ((reg) | (lchan_shift << 8) | (rchan_shift << 12) | \
  (mask << 16) | \
  (invert << 24) | \
  (stereo << 25) | \
  (enum_c << 26))

static void snd_azf3328_mixer_reg_decode(struct azf3328_mixer_reg *r, unsigned long val)
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

static int
snd_azf3328_info_mixer(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_info *uinfo)
{
	struct azf3328_mixer_reg reg;

	snd_azf3328_dbgcallenter();
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	uinfo->type = reg.mask == 1 ?
		SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = reg.stereo + 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = reg.mask;
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_get_mixer(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_azf3328 *chip = snd_kcontrol_chip(kcontrol);
	struct azf3328_mixer_reg reg;
	unsigned int oreg, val;

	snd_azf3328_dbgcallenter();
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);

	oreg = snd_azf3328_mixer_inw(chip, reg.reg);
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
	snd_azf3328_dbgmixer("get: %02x is %04x -> vol %02lx|%02lx "
			     "(shift %02d|%02d, mask %02x, inv. %d, stereo %d)\n",
		reg.reg, oreg,
		ucontrol->value.integer.value[0], ucontrol->value.integer.value[1],
		reg.lchan_shift, reg.rchan_shift, reg.mask, reg.invert, reg.stereo);
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_put_mixer(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_azf3328 *chip = snd_kcontrol_chip(kcontrol);
	struct azf3328_mixer_reg reg;
	unsigned int oreg, nreg, val;

	snd_azf3328_dbgcallenter();
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	oreg = snd_azf3328_mixer_inw(chip, reg.reg);
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
		snd_azf3328_mixer_write_volume_gradually(
			chip, reg.reg, nreg >> 8, nreg & 0xff,
			/* just set both channels, doesn't matter */
			SET_CHAN_LEFT|SET_CHAN_RIGHT,
			0);
	else
        	snd_azf3328_mixer_outw(chip, reg.reg, nreg);

	snd_azf3328_dbgmixer("put: %02x to %02lx|%02lx, "
			     "oreg %04x; shift %02d|%02d -> nreg %04x; after: %04x\n",
		reg.reg, ucontrol->value.integer.value[0], ucontrol->value.integer.value[1],
		oreg, reg.lchan_shift, reg.rchan_shift,
		nreg, snd_azf3328_mixer_inw(chip, reg.reg));
	snd_azf3328_dbgcallleave();
	return (nreg != oreg);
}

static int
snd_azf3328_info_mixer_enum(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts1[] = {
		"Mic1", "Mic2"
	};
	static const char * const texts2[] = {
		"Mix", "Mic"
	};
	static const char * const texts3[] = {
                "Mic", "CD", "Video", "Aux",
		"Line", "Mix", "Mix Mono", "Phone"
        };
	static const char * const texts4[] = {
		"pre 3D", "post 3D"
        };
	struct azf3328_mixer_reg reg;

	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
        uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
        uinfo->count = (reg.reg == IDX_MIXER_REC_SELECT) ? 2 : 1;
        uinfo->value.enumerated.items = reg.enum_c;
        if (uinfo->value.enumerated.item > reg.enum_c - 1U)
                uinfo->value.enumerated.item = reg.enum_c - 1U;
	if (reg.reg == IDX_MIXER_ADVCTL2) {
		switch(reg.lchan_shift) {
		case 8: /* modem out sel */
			strcpy(uinfo->value.enumerated.name, texts1[uinfo->value.enumerated.item]);
			break;
		case 9: /* mono sel source */
			strcpy(uinfo->value.enumerated.name, texts2[uinfo->value.enumerated.item]);
			break;
		case 15: /* PCM Out Path */
			strcpy(uinfo->value.enumerated.name, texts4[uinfo->value.enumerated.item]);
			break;
		}
	} else
        	strcpy(uinfo->value.enumerated.name, texts3[uinfo->value.enumerated.item]
);
        return 0;
}

static int
snd_azf3328_get_mixer_enum(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
        struct snd_azf3328 *chip = snd_kcontrol_chip(kcontrol);
	struct azf3328_mixer_reg reg;
        unsigned short val;
        
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	val = snd_azf3328_mixer_inw(chip, reg.reg);
	if (reg.reg == IDX_MIXER_REC_SELECT) {
        	ucontrol->value.enumerated.item[0] = (val >> 8) & (reg.enum_c - 1);
        	ucontrol->value.enumerated.item[1] = (val >> 0) & (reg.enum_c - 1);
	} else
        	ucontrol->value.enumerated.item[0] = (val >> reg.lchan_shift) & (reg.enum_c - 1);

	snd_azf3328_dbgmixer("get_enum: %02x is %04x -> %d|%d (shift %02d, enum_c %d)\n",
		reg.reg, val, ucontrol->value.enumerated.item[0], ucontrol->value.enumerated.item[1],
		reg.lchan_shift, reg.enum_c);
        return 0;
}

static int
snd_azf3328_put_mixer_enum(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
        struct snd_azf3328 *chip = snd_kcontrol_chip(kcontrol);
	struct azf3328_mixer_reg reg;
	unsigned int oreg, nreg, val;
        
	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	oreg = snd_azf3328_mixer_inw(chip, reg.reg);
	val = oreg;
	if (reg.reg == IDX_MIXER_REC_SELECT) {
        	if (ucontrol->value.enumerated.item[0] > reg.enum_c - 1U ||
            	ucontrol->value.enumerated.item[1] > reg.enum_c - 1U)
                	return -EINVAL;
        	val = (ucontrol->value.enumerated.item[0] << 8) |
        	      (ucontrol->value.enumerated.item[1] << 0);
	} else {
        	if (ucontrol->value.enumerated.item[0] > reg.enum_c - 1U)
                	return -EINVAL;
		val &= ~((reg.enum_c - 1) << reg.lchan_shift);
        	val |= (ucontrol->value.enumerated.item[0] << reg.lchan_shift);
	}
	snd_azf3328_mixer_outw(chip, reg.reg, val);
	nreg = val;

	snd_azf3328_dbgmixer("put_enum: %02x to %04x, oreg %04x\n", reg.reg, val, oreg);
	return (nreg != oreg);
}

static const struct snd_kcontrol_new snd_azf3328_mixer_controls[] __devinitdata = {
	AZF3328_MIXER_SWITCH("Master Playback Switch", IDX_MIXER_PLAY_MASTER, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Master Playback Volume", IDX_MIXER_PLAY_MASTER, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Wave Playback Switch", IDX_MIXER_WAVEOUT, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Wave Playback Volume", IDX_MIXER_WAVEOUT, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Wave 3D Bypass Playback Switch", IDX_MIXER_ADVCTL2, 7, 1),
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
	AZF3328_MIXER_SWITCH("PC Speaker Playback Switch", IDX_MIXER_PCBEEP, 15, 1),
	AZF3328_MIXER_VOL_SPECIAL("PC Speaker Playback Volume", IDX_MIXER_PCBEEP, 0x0f, 1, 1),
	AZF3328_MIXER_SWITCH("Video Playback Switch", IDX_MIXER_VIDEO, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Video Playback Volume", IDX_MIXER_VIDEO, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Aux Playback Switch", IDX_MIXER_AUX, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Aux Playback Volume", IDX_MIXER_AUX, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Modem Playback Switch", IDX_MIXER_MODEMOUT, 15, 1),
	AZF3328_MIXER_VOL_MONO("Modem Playback Volume", IDX_MIXER_MODEMOUT, 0x1f, 1),
	AZF3328_MIXER_SWITCH("Modem Capture Switch", IDX_MIXER_MODEMIN, 15, 1),
	AZF3328_MIXER_VOL_MONO("Modem Capture Volume", IDX_MIXER_MODEMIN, 0x1f, 1),
	AZF3328_MIXER_ENUM("Mic Select", IDX_MIXER_ADVCTL2, 2, 8),
	AZF3328_MIXER_ENUM("Mono Output Select", IDX_MIXER_ADVCTL2, 2, 9),
	AZF3328_MIXER_ENUM("PCM", IDX_MIXER_ADVCTL2, 2, 15), /* PCM Out Path, place in front since it controls *both* 3D and Bass/Treble! */
	AZF3328_MIXER_VOL_SPECIAL("Tone Control - Treble", IDX_MIXER_BASSTREBLE, 0x07, 1, 0),
	AZF3328_MIXER_VOL_SPECIAL("Tone Control - Bass", IDX_MIXER_BASSTREBLE, 0x07, 9, 0),
	AZF3328_MIXER_SWITCH("3D Control - Switch", IDX_MIXER_ADVCTL2, 13, 0),
	AZF3328_MIXER_VOL_SPECIAL("3D Control - Width", IDX_MIXER_ADVCTL1, 0x07, 1, 0), /* "3D Width" */
	AZF3328_MIXER_VOL_SPECIAL("3D Control - Depth", IDX_MIXER_ADVCTL1, 0x03, 8, 0), /* "Hifi 3D" */
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

static const u16 __devinitdata snd_azf3328_init_values[][2] = {
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

static int __devinit
snd_azf3328_mixer_new(struct snd_azf3328 *chip)
{
	struct snd_card *card;
	const struct snd_kcontrol_new *sw;
	unsigned int idx;
	int err;

	snd_azf3328_dbgcallenter();
	snd_assert(chip != NULL && chip->card != NULL, return -EINVAL);

	card = chip->card;

	/* mixer reset */
	snd_azf3328_mixer_outw(chip, IDX_MIXER_RESET, 0x0000);

	/* mute and zero volume channels */
	for (idx = 0; idx < ARRAY_SIZE(snd_azf3328_init_values); idx++) {
		snd_azf3328_mixer_outw(chip,
			snd_azf3328_init_values[idx][0],
			snd_azf3328_init_values[idx][1]);
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

static int
snd_azf3328_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	int res;
	snd_azf3328_dbgcallenter();
	res = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	snd_azf3328_dbgcallleave();
	return res;
}

static int
snd_azf3328_hw_free(struct snd_pcm_substream *substream)
{
	snd_azf3328_dbgcallenter();
	snd_pcm_lib_free_pages(substream);
	snd_azf3328_dbgcallleave();
	return 0;
}

static void
snd_azf3328_setfmt(struct snd_azf3328 *chip,
			       unsigned int reg,
			       unsigned int bitrate,
			       unsigned int format_width,
			       unsigned int channels
)
{
	u16 val = 0xff00;
	unsigned long flags;

	snd_azf3328_dbgcallenter();
	switch (bitrate) {
	case  4000: val |= SOUNDFORMAT_FREQ_SUSPECTED_4000; break;
	case  4800: val |= SOUNDFORMAT_FREQ_SUSPECTED_4800; break;
	case  5512: val |= SOUNDFORMAT_FREQ_5510; break; /* the AZF3328 names it "5510" for some strange reason */
	case  6620: val |= SOUNDFORMAT_FREQ_6620; break;
	case  8000: val |= SOUNDFORMAT_FREQ_8000; break;
	case  9600: val |= SOUNDFORMAT_FREQ_9600; break;
	case 11025: val |= SOUNDFORMAT_FREQ_11025; break;
	case 13240: val |= SOUNDFORMAT_FREQ_SUSPECTED_13240; break;
	case 16000: val |= SOUNDFORMAT_FREQ_16000; break;
	case 22050: val |= SOUNDFORMAT_FREQ_22050; break;
	case 32000: val |= SOUNDFORMAT_FREQ_32000; break;
	case 44100: val |= SOUNDFORMAT_FREQ_44100; break;
	case 48000: val |= SOUNDFORMAT_FREQ_48000; break;
	case 66200: val |= SOUNDFORMAT_FREQ_SUSPECTED_66200; break;
	default:
		snd_printk(KERN_WARNING "unknown bitrate %d, assuming 44.1kHz!\n", bitrate);
		val |= SOUNDFORMAT_FREQ_44100;
		break;
	}
	/* val = 0xff07; 3m27.993s (65301Hz; -> 64000Hz???) hmm, 66120, 65967, 66123 */
	/* val = 0xff09; 17m15.098s (13123,478Hz; -> 12000Hz???) hmm, 13237.2Hz? */
	/* val = 0xff0a; 47m30.599s (4764,891Hz; -> 4800Hz???) yup, 4803Hz */
	/* val = 0xff0c; 57m0.510s (4010,263Hz; -> 4000Hz???) yup, 4003Hz */
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
	snd_azf3328_codec_outw(chip, reg, val);
	
	/* changing the bitrate/format settings switches off the
	 * audio output with an annoying click in case of 8/16bit format change
	 * (maybe shutting down DAC/ADC?), thus immediately
	 * do some tweaking to reenable it and get rid of the clicking
	 * (FIXME: yes, it works, but what exactly am I doing here?? :)
	 * FIXME: does this have some side effects for full-duplex
	 * or other dramatic side effects? */
	if (reg == IDX_IO_PLAY_SOUNDFORMAT) /* only do it for playback */
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS,
			snd_azf3328_codec_inw(chip, IDX_IO_PLAY_FLAGS) |
			DMA_PLAY_SOMETHING1 |
			DMA_PLAY_SOMETHING2 |
			SOMETHING_ALMOST_ALWAYS_SET |
			DMA_EPILOGUE_SOMETHING |
			DMA_SOMETHING_ELSE
		);

	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_azf3328_dbgcallleave();
}

static void
snd_azf3328_setdmaa(struct snd_azf3328 *chip,
				long unsigned int addr,
                                unsigned int count,
                                unsigned int size,
				int do_recording)
{
	unsigned long flags, portbase;
	unsigned int is_running;

	snd_azf3328_dbgcallenter();
	if (do_recording) {
		/* access capture registers, i.e. skip playback reg section */
		portbase = chip->codec_port + 0x20;
		is_running = chip->is_recording;
	} else {
		/* access the playback register section */
		portbase = chip->codec_port + 0x00;
		is_running = chip->is_playing;
	}

	/* AZF3328 uses a two buffer pointer DMA playback approach */
	if (!is_running) {
		unsigned long addr_area2;
		unsigned long count_areas, count_tmp; /* width 32bit -- overflow!! */
		count_areas = size/2;
		addr_area2 = addr+count_areas;
		count_areas--; /* max. index */
		snd_azf3328_dbgplay("set DMA: buf1 %08lx[%lu], buf2 %08lx[%lu]\n", addr, count_areas, addr_area2, count_areas);

		/* build combined I/O buffer length word */
		count_tmp = count_areas;
		count_areas |= (count_tmp << 16);
		spin_lock_irqsave(&chip->reg_lock, flags);
		outl(addr, portbase + IDX_IO_PLAY_DMA_START_1);
		outl(addr_area2, portbase + IDX_IO_PLAY_DMA_START_2);
		outl(count_areas, portbase + IDX_IO_PLAY_DMA_LEN_1);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
	snd_azf3328_dbgcallleave();
}

static int
snd_azf3328_playback_prepare(struct snd_pcm_substream *substream)
{
#if 0
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
        unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);
#endif

	snd_azf3328_dbgcallenter();
#if 0
	snd_azf3328_setfmt(chip, IDX_IO_PLAY_SOUNDFORMAT,
		runtime->rate,
		snd_pcm_format_width(runtime->format),
		runtime->channels);
	snd_azf3328_setdmaa(chip, runtime->dma_addr, count, size, 0);
#endif
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_capture_prepare(struct snd_pcm_substream *substream)
{
#if 0
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
        unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);
#endif

	snd_azf3328_dbgcallenter();
#if 0
	snd_azf3328_setfmt(chip, IDX_IO_REC_SOUNDFORMAT,
		runtime->rate,
		snd_pcm_format_width(runtime->format),
		runtime->channels);
	snd_azf3328_setdmaa(chip, runtime->dma_addr, count, size, 1);
#endif
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int result = 0;
	unsigned int status1;

	snd_azf3328_dbgcalls("snd_azf3328_playback_trigger cmd %d\n", cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_azf3328_dbgplay("START PLAYBACK\n");

		/* mute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 1);

		snd_azf3328_setfmt(chip, IDX_IO_PLAY_SOUNDFORMAT,
			runtime->rate,
			snd_pcm_format_width(runtime->format),
			runtime->channels);

		spin_lock(&chip->reg_lock);
		/* stop playback */
		status1 = snd_azf3328_codec_inw(chip, IDX_IO_PLAY_FLAGS);
		status1 &= ~DMA_RESUME;
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS, status1);
	    
		/* FIXME: clear interrupts or what??? */
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_IRQTYPE, 0xffff);
		spin_unlock(&chip->reg_lock);

		snd_azf3328_setdmaa(chip, runtime->dma_addr,
			snd_pcm_lib_period_bytes(substream),
			snd_pcm_lib_buffer_bytes(substream),
			0);

		spin_lock(&chip->reg_lock);
#ifdef WIN9X
		/* FIXME: enable playback/recording??? */
		status1 |= DMA_PLAY_SOMETHING1 | DMA_PLAY_SOMETHING2;
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS, status1);

		/* start playback again */
		/* FIXME: what is this value (0x0010)??? */
		status1 |= DMA_RESUME | DMA_EPILOGUE_SOMETHING;
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS, status1);
#else /* NT4 */
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS,
			0x0000);
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS,
			DMA_PLAY_SOMETHING1);
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS,
			DMA_PLAY_SOMETHING1 |
			DMA_PLAY_SOMETHING2);
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS,
			DMA_RESUME |
			SOMETHING_ALMOST_ALWAYS_SET |
			DMA_EPILOGUE_SOMETHING |
			DMA_SOMETHING_ELSE);
#endif
		spin_unlock(&chip->reg_lock);

		/* now unmute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 0);

		chip->is_playing = 1;
		snd_azf3328_dbgplay("STARTED PLAYBACK\n");
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		snd_azf3328_dbgplay("RESUME PLAYBACK\n");
		/* resume playback if we were active */
		if (chip->is_playing)
			snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS,
				snd_azf3328_codec_inw(chip, IDX_IO_PLAY_FLAGS) | DMA_RESUME);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_azf3328_dbgplay("STOP PLAYBACK\n");

		/* mute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 1);

		spin_lock(&chip->reg_lock);
		/* stop playback */
		status1 = snd_azf3328_codec_inw(chip, IDX_IO_PLAY_FLAGS);

		status1 &= ~DMA_RESUME;
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS, status1);

		/* hmm, is this really required? we're resetting the same bit
		 * immediately thereafter... */
		status1 |= DMA_PLAY_SOMETHING1;
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS, status1);

		status1 &= ~DMA_PLAY_SOMETHING1;
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS, status1);
		spin_unlock(&chip->reg_lock);
	    
		/* now unmute WaveOut */
		snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 0);
		chip->is_playing = 0;
		snd_azf3328_dbgplay("STOPPED PLAYBACK\n");
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_azf3328_dbgplay("SUSPEND PLAYBACK\n");
		/* make sure playback is stopped */
		snd_azf3328_codec_outw(chip, IDX_IO_PLAY_FLAGS,
			snd_azf3328_codec_inw(chip, IDX_IO_PLAY_FLAGS) & ~DMA_RESUME);
		break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_printk(KERN_ERR "FIXME: SNDRV_PCM_TRIGGER_PAUSE_PUSH NIY!\n");
                break;
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_printk(KERN_ERR "FIXME: SNDRV_PCM_TRIGGER_PAUSE_RELEASE NIY!\n");
                break;
        default:
		printk(KERN_ERR "FIXME: unknown trigger mode!\n");
                return -EINVAL;
	}
	
	snd_azf3328_dbgcallleave();
	return result;
}

/* this is just analogous to playback; I'm not quite sure whether recording
 * should actually be triggered like that */
static int
snd_azf3328_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int result = 0;
	unsigned int status1;

	snd_azf3328_dbgcalls("snd_azf3328_capture_trigger cmd %d\n", cmd);

        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:

		snd_azf3328_dbgplay("START CAPTURE\n");

		snd_azf3328_setfmt(chip, IDX_IO_REC_SOUNDFORMAT,
			runtime->rate,
			snd_pcm_format_width(runtime->format),
			runtime->channels);

		spin_lock(&chip->reg_lock);
		/* stop recording */
		status1 = snd_azf3328_codec_inw(chip, IDX_IO_REC_FLAGS);
		status1 &= ~DMA_RESUME;
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS, status1);
	    
		/* FIXME: clear interrupts or what??? */
		snd_azf3328_codec_outw(chip, IDX_IO_REC_IRQTYPE, 0xffff);
		spin_unlock(&chip->reg_lock);

		snd_azf3328_setdmaa(chip, runtime->dma_addr,
			snd_pcm_lib_period_bytes(substream),
			snd_pcm_lib_buffer_bytes(substream),
			1);

		spin_lock(&chip->reg_lock);
#ifdef WIN9X
		/* FIXME: enable playback/recording??? */
		status1 |= DMA_PLAY_SOMETHING1 | DMA_PLAY_SOMETHING2;
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS, status1);

		/* start capture again */
		/* FIXME: what is this value (0x0010)??? */
		status1 |= DMA_RESUME | DMA_EPILOGUE_SOMETHING;
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS, status1);
#else
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS,
			0x0000);
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS,
			DMA_PLAY_SOMETHING1);
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS,
			DMA_PLAY_SOMETHING1 |
			DMA_PLAY_SOMETHING2);
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS,
			DMA_RESUME |
			SOMETHING_ALMOST_ALWAYS_SET |
			DMA_EPILOGUE_SOMETHING |
			DMA_SOMETHING_ELSE);
#endif
		spin_unlock(&chip->reg_lock);

		chip->is_recording = 1;
		snd_azf3328_dbgplay("STARTED CAPTURE\n");
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		snd_azf3328_dbgplay("RESUME CAPTURE\n");
		/* resume recording if we were active */
		if (chip->is_recording)
			snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS,
				snd_azf3328_codec_inw(chip, IDX_IO_REC_FLAGS) | DMA_RESUME);
		break;
        case SNDRV_PCM_TRIGGER_STOP:
		snd_azf3328_dbgplay("STOP CAPTURE\n");

		spin_lock(&chip->reg_lock);
		/* stop recording */
		status1 = snd_azf3328_codec_inw(chip, IDX_IO_REC_FLAGS);

		status1 &= ~DMA_RESUME;
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS, status1);

		status1 |= DMA_PLAY_SOMETHING1;
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS, status1);

		status1 &= ~DMA_PLAY_SOMETHING1;
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS, status1);
		spin_unlock(&chip->reg_lock);
	    
		chip->is_recording = 0;
		snd_azf3328_dbgplay("STOPPED CAPTURE\n");
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_azf3328_dbgplay("SUSPEND CAPTURE\n");
		/* make sure recording is stopped */
		snd_azf3328_codec_outw(chip, IDX_IO_REC_FLAGS,
			snd_azf3328_codec_inw(chip, IDX_IO_REC_FLAGS) & ~DMA_RESUME);
		break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_printk(KERN_ERR "FIXME: SNDRV_PCM_TRIGGER_PAUSE_PUSH NIY!\n");
                break;
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_printk(KERN_ERR "FIXME: SNDRV_PCM_TRIGGER_PAUSE_RELEASE NIY!\n");
                break;
        default:
		printk(KERN_ERR "FIXME: unknown trigger mode!\n");
                return -EINVAL;
	}
	
	snd_azf3328_dbgcallleave();
	return result;
}

static snd_pcm_uframes_t
snd_azf3328_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	unsigned long bufptr, result;
	snd_pcm_uframes_t frmres;

#ifdef QUERY_HARDWARE
	bufptr = inl(chip->codec_port+IDX_IO_PLAY_DMA_START_1);
#else
	bufptr = substream->runtime->dma_addr;
#endif
	result = inl(chip->codec_port+IDX_IO_PLAY_DMA_CURRPOS);

	/* calculate offset */
	result -= bufptr;
	frmres = bytes_to_frames( substream->runtime, result);
	snd_azf3328_dbgplay("PLAY @ 0x%8lx, frames %8ld\n", result, frmres);
	return frmres;
}

static snd_pcm_uframes_t
snd_azf3328_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	unsigned long bufptr, result;
	snd_pcm_uframes_t frmres;

#ifdef QUERY_HARDWARE
	bufptr = inl(chip->codec_port+IDX_IO_REC_DMA_START_1);
#else
	bufptr = substream->runtime->dma_addr;
#endif
	result = inl(chip->codec_port+IDX_IO_REC_DMA_CURRPOS);

	/* calculate offset */
	result -= bufptr;
	frmres = bytes_to_frames( substream->runtime, result);
	snd_azf3328_dbgplay("REC  @ 0x%8lx, frames %8ld\n", result, frmres);
	return frmres;
}

static irqreturn_t
snd_azf3328_interrupt(int irq, void *dev_id)
{
	struct snd_azf3328 *chip = dev_id;
	u8 status, which;
	static unsigned long irq_count;

	status = snd_azf3328_codec_inb(chip, IDX_IO_IRQSTATUS);

        /* fast path out, to ease interrupt sharing */
	if (!(status & (IRQ_PLAYBACK|IRQ_RECORDING|IRQ_MPU401|IRQ_TIMER)))
		return IRQ_NONE; /* must be interrupt for another device */

	snd_azf3328_dbgplay("Interrupt %ld!\nIDX_IO_PLAY_FLAGS %04x, IDX_IO_PLAY_IRQTYPE %04x, IDX_IO_IRQSTATUS %04x\n",
		irq_count,
		snd_azf3328_codec_inw(chip, IDX_IO_PLAY_FLAGS),
		snd_azf3328_codec_inw(chip, IDX_IO_PLAY_IRQTYPE),
		status);
		
	if (status & IRQ_TIMER) {
		/* snd_azf3328_dbgplay("timer %ld\n", inl(chip->codec_port+IDX_IO_TIMER_VALUE) & TIMER_VALUE_MASK); */
		if (chip->timer)
			snd_timer_interrupt(chip->timer, chip->timer->sticks);
		/* ACK timer */
                spin_lock(&chip->reg_lock);
		snd_azf3328_codec_outb(chip, IDX_IO_TIMER_VALUE + 3, 0x07);
		spin_unlock(&chip->reg_lock);
		snd_azf3328_dbgplay("azt3328: timer IRQ\n");
	}
	if (status & IRQ_PLAYBACK) {
		spin_lock(&chip->reg_lock);
		which = snd_azf3328_codec_inb(chip, IDX_IO_PLAY_IRQTYPE);
		/* ack all IRQ types immediately */
		snd_azf3328_codec_outb(chip, IDX_IO_PLAY_IRQTYPE, which);
               	spin_unlock(&chip->reg_lock);

		if (chip->pcm && chip->playback_substream) {
			snd_pcm_period_elapsed(chip->playback_substream);
			snd_azf3328_dbgplay("PLAY period done (#%x), @ %x\n",
				which,
				inl(chip->codec_port+IDX_IO_PLAY_DMA_CURRPOS));
		} else
			snd_azf3328_dbgplay("azt3328: ouch, irq handler problem!\n");
		if (which & IRQ_PLAY_SOMETHING)
			snd_azf3328_dbgplay("azt3328: unknown play IRQ type occurred, please report!\n");
	}
	if (status & IRQ_RECORDING) {
                spin_lock(&chip->reg_lock);
		which = snd_azf3328_codec_inb(chip, IDX_IO_REC_IRQTYPE);
		/* ack all IRQ types immediately */
		snd_azf3328_codec_outb(chip, IDX_IO_REC_IRQTYPE, which);
		spin_unlock(&chip->reg_lock);

		if (chip->pcm && chip->capture_substream) {
			snd_pcm_period_elapsed(chip->capture_substream);
			snd_azf3328_dbgplay("REC  period done (#%x), @ %x\n",
				which,
				inl(chip->codec_port+IDX_IO_REC_DMA_CURRPOS));
		} else
			snd_azf3328_dbgplay("azt3328: ouch, irq handler problem!\n");
		if (which & IRQ_REC_SOMETHING)
			snd_azf3328_dbgplay("azt3328: unknown rec IRQ type occurred, please report!\n");
	}
	/* MPU401 has less critical IRQ requirements
	 * than timer and playback/recording, right? */
	if (status & IRQ_MPU401) {
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data);

		/* hmm, do we have to ack the IRQ here somehow?
		 * If so, then I don't know how... */
		snd_azf3328_dbgplay("azt3328: MPU401 IRQ\n");
	}
	irq_count++;
	return IRQ_HANDLED;
}

/*****************************************************************/

static const struct snd_pcm_hardware snd_azf3328_playback =
{
	/* FIXME!! Correct? */
	.info =			SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_MMAP_VALID,
	.formats =		SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_U8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_U16_LE,
	.rates =		SNDRV_PCM_RATE_5512 |
				SNDRV_PCM_RATE_8000_48000 |
				SNDRV_PCM_RATE_KNOT,
	.rate_min =		4000,
	.rate_max =		66200,
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

static const struct snd_pcm_hardware snd_azf3328_capture =
{
	/* FIXME */
	.info =			SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_MMAP_VALID,
	.formats =		SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_U8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_U16_LE,
	.rates =		SNDRV_PCM_RATE_5512 |
				SNDRV_PCM_RATE_8000_48000 |
				SNDRV_PCM_RATE_KNOT,
	.rate_min =		4000,
	.rate_max =		66200,
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
	4000, 4800, 5512, 6620, 8000, 9600, 11025, 13240, 16000, 22050, 32000,
	44100, 48000, 66200 };
static struct snd_pcm_hw_constraint_list snd_azf3328_hw_constraints_rates = {
	.count = ARRAY_SIZE(snd_azf3328_fixed_rates), 
	.list = snd_azf3328_fixed_rates,
	.mask = 0,
};

/*****************************************************************/

static int
snd_azf3328_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_azf3328_dbgcallenter();
	chip->playback_substream = substream;
	runtime->hw = snd_azf3328_playback;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &snd_azf3328_hw_constraints_rates);
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_azf3328_dbgcallenter();
	chip->capture_substream = substream;
	runtime->hw = snd_azf3328_capture;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &snd_azf3328_hw_constraints_rates);
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);

	snd_azf3328_dbgcallenter();

	chip->playback_substream = NULL;
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);

	snd_azf3328_dbgcallenter();
	chip->capture_substream = NULL;
	snd_azf3328_dbgcallleave();
	return 0;
}

/******************************************************************/

static struct snd_pcm_ops snd_azf3328_playback_ops = {
	.open =		snd_azf3328_playback_open,
	.close =	snd_azf3328_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_azf3328_hw_params,
	.hw_free =	snd_azf3328_hw_free,
	.prepare =	snd_azf3328_playback_prepare,
	.trigger =	snd_azf3328_playback_trigger,
	.pointer =	snd_azf3328_playback_pointer
};

static struct snd_pcm_ops snd_azf3328_capture_ops = {
	.open =		snd_azf3328_capture_open,
	.close =	snd_azf3328_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_azf3328_hw_params,
	.hw_free =	snd_azf3328_hw_free,
	.prepare =	snd_azf3328_capture_prepare,
	.trigger =	snd_azf3328_capture_trigger,
	.pointer =	snd_azf3328_capture_pointer
};

static int __devinit
snd_azf3328_pcm(struct snd_azf3328 *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	snd_azf3328_dbgcallenter();
	if ((err = snd_pcm_new(chip->card, "AZF3328 DSP", device, 1, 1, &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_azf3328_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_azf3328_capture_ops);

	pcm->private_data = chip;
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
static int __devinit
snd_azf3328_config_joystick(struct snd_azf3328 *chip, int dev)
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
		release_and_free_resource(r);
		return -ENOMEM;
	}

	gameport_set_name(gp, "AZF3328 Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gp->io = 0x200;
	gameport_set_port_data(gp, r);

	snd_azf3328_io2_outb(chip, IDX_IO2_LEGACY_ADDR,
			      snd_azf3328_io2_inb(chip, IDX_IO2_LEGACY_ADDR) | LEGACY_JOY);

	gameport_register_port(chip->gameport);

	return 0;
}

static void
snd_azf3328_free_joystick(struct snd_azf3328 *chip)
{
	if (chip->gameport) {
		struct resource *r = gameport_get_port_data(chip->gameport);

		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
		/* disable gameport */
		snd_azf3328_io2_outb(chip, IDX_IO2_LEGACY_ADDR,
				      snd_azf3328_io2_inb(chip, IDX_IO2_LEGACY_ADDR) & ~LEGACY_JOY);
		release_and_free_resource(r);
	}
}
#else
static inline int
snd_azf3328_config_joystick(struct snd_azf3328 *chip, int dev) { return -ENOSYS; }
static inline void
snd_azf3328_free_joystick(struct snd_azf3328 *chip) { }
#endif

/******************************************************************/

static int
snd_azf3328_free(struct snd_azf3328 *chip)
{
        if (chip->irq < 0)
                goto __end_hw;

	/* reset (close) mixer */
	snd_azf3328_mixer_set_mute(chip, IDX_MIXER_PLAY_MASTER, 1); /* first mute master volume */
	snd_azf3328_mixer_outw(chip, IDX_MIXER_RESET, 0x0000);

        /* interrupt setup - mask everything (FIXME!) */
	/* well, at least we know how to disable the timer IRQ */
	snd_azf3328_codec_outb(chip, IDX_IO_TIMER_VALUE + 3, 0x00);

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

static int
snd_azf3328_dev_free(struct snd_device *device)
{
	struct snd_azf3328 *chip = device->device_data;
	return snd_azf3328_free(chip);
}

/******************************************************************/

/*** NOTE: the physical timer resolution actually is 1024000 ticks per second,
 *** but announcing those attributes to user-space would make programs
 *** configure the timer to a 1 tick value, resulting in an absolutely fatal
 *** timer IRQ storm.
 *** Thus I chose to announce a down-scaled virtual timer to the outside and
 *** calculate real timer countdown values internally.
 *** (the scale factor can be set via module parameter "seqtimer_scaling").
 ***/

static int
snd_azf3328_timer_start(struct snd_timer *timer)
{
	struct snd_azf3328 *chip;
	unsigned long flags;
	unsigned int delay;

	snd_azf3328_dbgcallenter();
	chip = snd_timer_chip(timer);
	delay = ((timer->sticks * seqtimer_scaling) - 1) & TIMER_VALUE_MASK;
	if (delay < 49) {
		/* uhoh, that's not good, since user-space won't know about
		 * this timing tweak
		 * (we need to do it to avoid a lockup, though) */

		snd_azf3328_dbgtimer("delay was too low (%d)!\n", delay);
		delay = 49; /* minimum time is 49 ticks */
	}
	snd_azf3328_dbgtimer("setting timer countdown value %d, add COUNTDOWN|IRQ\n", delay);
	delay |= TIMER_ENABLE_COUNTDOWN | TIMER_ENABLE_IRQ;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_azf3328_codec_outl(chip, IDX_IO_TIMER_VALUE, delay);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_azf3328_dbgcallleave();
	return 0;
}

static int
snd_azf3328_timer_stop(struct snd_timer *timer)
{
	struct snd_azf3328 *chip;
	unsigned long flags;

	snd_azf3328_dbgcallenter();
	chip = snd_timer_chip(timer);
	spin_lock_irqsave(&chip->reg_lock, flags);
	/* disable timer countdown and interrupt */
	/* FIXME: should we write TIMER_ACK_IRQ here? */
	snd_azf3328_codec_outb(chip, IDX_IO_TIMER_VALUE + 3, 0);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_azf3328_dbgcallleave();
	return 0;
}


static int
snd_azf3328_timer_precise_resolution(struct snd_timer *timer,
					       unsigned long *num, unsigned long *den)
{
	snd_azf3328_dbgcallenter();
	*num = 1;
	*den = 1024000 / seqtimer_scaling;
	snd_azf3328_dbgcallleave();
	return 0;
}

static struct snd_timer_hardware snd_azf3328_timer_hw = {
	.flags = SNDRV_TIMER_HW_AUTO,
	.resolution = 977, /* 1000000/1024000 = 0.9765625us */
	.ticks = 1024000, /* max tick count, defined by the value register; actually it's not 1024000, but 1048576, but we don't care */
	.start = snd_azf3328_timer_start,
	.stop = snd_azf3328_timer_stop,
	.precise_resolution = snd_azf3328_timer_precise_resolution,
};

static int __devinit
snd_azf3328_timer(struct snd_azf3328 *chip, int device)
{
	struct snd_timer *timer = NULL;
	struct snd_timer_id tid;
	int err;

	snd_azf3328_dbgcallenter();
	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = chip->card->number;
	tid.device = device;
	tid.subdevice = 0;

	snd_azf3328_timer_hw.resolution *= seqtimer_scaling;
	snd_azf3328_timer_hw.ticks /= seqtimer_scaling;
	if ((err = snd_timer_new(chip->card, "AZF3328", &tid, &timer)) < 0) {
		goto out;
	}

	strcpy(timer->name, "AZF3328 timer");
	timer->private_data = chip;
	timer->hw = snd_azf3328_timer_hw;

	chip->timer = timer;

	err = 0;

out:
	snd_azf3328_dbgcallleave();
	return err;
}

/******************************************************************/

#if 0
/* check whether a bit can be modified */
static void
snd_azf3328_test_bit(unsigned int reg, int bit)
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

static void
snd_azf3328_debug_show_ports(const struct snd_azf3328 *chip)
{
#if DEBUG_MISC
	u16 tmp;

	snd_azf3328_dbgmisc("codec_port 0x%lx, io2_port 0x%lx, mpu_port 0x%lx, synth_port 0x%lx, mixer_port 0x%lx, irq %d\n", chip->codec_port, chip->io2_port, chip->mpu_port, chip->synth_port, chip->mixer_port, chip->irq);

	snd_azf3328_dbgmisc("io2 %02x %02x %02x %02x %02x %02x\n", snd_azf3328_io2_inb(chip, 0), snd_azf3328_io2_inb(chip, 1), snd_azf3328_io2_inb(chip, 2), snd_azf3328_io2_inb(chip, 3), snd_azf3328_io2_inb(chip, 4), snd_azf3328_io2_inb(chip, 5));

	for (tmp=0; tmp <= 0x01; tmp += 1)
		snd_azf3328_dbgmisc("0x%02x: opl 0x%04x, mpu300 0x%04x, mpu310 0x%04x, mpu320 0x%04x, mpu330 0x%04x\n", tmp, inb(0x388 + tmp), inb(0x300 + tmp), inb(0x310 + tmp), inb(0x320 + tmp), inb(0x330 + tmp));

	for (tmp = 0; tmp <= 0x6E; tmp += 2)
		snd_azf3328_dbgmisc("0x%02x: 0x%04x\n", tmp, snd_azf3328_codec_inb(chip, tmp));
#endif
}

static int __devinit
snd_azf3328_create(struct snd_card *card,
                                         struct pci_dev *pci,
                                         unsigned long device_type,
                                         struct snd_azf3328 ** rchip)
{
	struct snd_azf3328 *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =     snd_azf3328_dev_free,
	};
	u16 tmp;

	*rchip = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		err = -ENOMEM;
		goto out_err;
	}
	spin_lock_init(&chip->reg_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	/* check if we can restrict PCI DMA transfers to 24 bits */
	if (pci_set_dma_mask(pci, DMA_24BIT_MASK) < 0 ||
	    pci_set_consistent_dma_mask(pci, DMA_24BIT_MASK) < 0) {
		snd_printk(KERN_ERR "architecture does not support 24bit PCI busmaster DMA\n");
		err = -ENXIO;
		goto out_err;
	}

	if ((err = pci_request_regions(pci, "Aztech AZF3328")) < 0) {
		goto out_err;
	}

	chip->codec_port = pci_resource_start(pci, 0);
	chip->io2_port   = pci_resource_start(pci, 1);
	chip->mpu_port   = pci_resource_start(pci, 2);
	chip->synth_port = pci_resource_start(pci, 3);
	chip->mixer_port = pci_resource_start(pci, 4);

	if (request_irq(pci->irq, snd_azf3328_interrupt, IRQF_DISABLED|IRQF_SHARED, card->shortname, (void *)chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		err = -EBUSY;
		goto out_err;
	}
	chip->irq = pci->irq;
	pci_set_master(pci);
	synchronize_irq(chip->irq);

	snd_azf3328_debug_show_ports(chip);
	
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		goto out_err;
	}

	/* create mixer interface & switches */
	if ((err = snd_azf3328_mixer_new(chip)) < 0)
		goto out_err;

#if 0
	/* set very low bitrate to reduce noise and power consumption? */
	snd_azf3328_setfmt(chip, IDX_IO_PLAY_SOUNDFORMAT, 5512, 8, 1);
#endif

	/* standard chip init stuff */
	/* default IRQ init value */
	tmp = DMA_PLAY_SOMETHING2|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE;

	spin_lock_irq(&chip->reg_lock);
	snd_azf3328_codec_outb(chip, IDX_IO_PLAY_FLAGS, tmp);
	snd_azf3328_codec_outb(chip, IDX_IO_REC_FLAGS, tmp);
	snd_azf3328_codec_outb(chip, IDX_IO_SOMETHING_FLAGS, tmp);
	snd_azf3328_codec_outb(chip, IDX_IO_TIMER_VALUE + 3, 0x00); /* disable timer */
	spin_unlock_irq(&chip->reg_lock);

	snd_card_set_dev(card, &pci->dev);

	*rchip = chip;

	err = 0;
	goto out;

out_err:
	if (chip)
		snd_azf3328_free(chip);
	pci_disable_device(pci);

out:
	return err;
}

static int __devinit
snd_azf3328_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_azf3328 *chip;
	struct snd_opl3 *opl3;
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
		goto out_err;
	}

	card->private_data = chip;

	if ((err = snd_mpu401_uart_new( card, 0, MPU401_HW_MPU401,
				        chip->mpu_port, MPU401_INFO_INTEGRATED,
					pci->irq, 0, &chip->rmidi)) < 0) {
		snd_printk(KERN_ERR "azf3328: no MPU-401 device at 0x%lx?\n", chip->mpu_port);
		goto out_err;
	}

	if ((err = snd_azf3328_timer(chip, 0)) < 0) {
		goto out_err;
	}

	if ((err = snd_azf3328_pcm(chip, 0)) < 0) {
		goto out_err;
	}

	if (snd_opl3_create(card, chip->synth_port, chip->synth_port+2,
			    OPL3_HW_AUTO, 1, &opl3) < 0) {
		snd_printk(KERN_ERR "azf3328: no OPL3 device at 0x%lx-0x%lx?\n",
			   chip->synth_port, chip->synth_port+2 );
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			goto out_err;
		}
	}

	opl3->private_data = chip;

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->codec_port, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		goto out_err;
	}

#ifdef MODULE
	printk(
"azt3328: Sound driver for Aztech AZF3328-based soundcards such as PCI168\n"
"azt3328: (hardware was completely undocumented - ZERO support from Aztech).\n"
"azt3328: Feel free to contact andi AT lisas.de for bug reports etc.!\n"
"azt3328: User-scalable sequencer timer set to %dHz (1024000Hz / %d).\n",
	1024000 / seqtimer_scaling, seqtimer_scaling);
#endif

	if (snd_azf3328_config_joystick(chip, dev) < 0)
		snd_azf3328_io2_outb(chip, IDX_IO2_LEGACY_ADDR,
			      snd_azf3328_io2_inb(chip, IDX_IO2_LEGACY_ADDR) & ~LEGACY_JOY);

	pci_set_drvdata(pci, card);
	dev++;

	err = 0;
	goto out;
	
out_err:
	snd_card_free(card);
	
out:
	snd_azf3328_dbgcallleave();
	return err;
}

static void __devexit
snd_azf3328_remove(struct pci_dev *pci)
{
	snd_azf3328_dbgcallenter();
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
	snd_azf3328_dbgcallleave();
}

#ifdef CONFIG_PM
static int
snd_azf3328_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_azf3328 *chip = card->private_data;
	int reg;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	
	snd_pcm_suspend_all(chip->pcm);

	for (reg = 0; reg < AZF_IO_SIZE_MIXER_PM / 2; reg++)
		chip->saved_regs_mixer[reg] = inw(chip->mixer_port + reg * 2);

	/* make sure to disable master volume etc. to prevent looping sound */
	snd_azf3328_mixer_set_mute(chip, IDX_MIXER_PLAY_MASTER, 1);
	snd_azf3328_mixer_set_mute(chip, IDX_MIXER_WAVEOUT, 1);
	
	for (reg = 0; reg < AZF_IO_SIZE_CODEC_PM / 2; reg++)
		chip->saved_regs_codec[reg] = inw(chip->codec_port + reg * 2);
	for (reg = 0; reg < AZF_IO_SIZE_IO2_PM / 2; reg++)
		chip->saved_regs_io2[reg] = inw(chip->io2_port + reg * 2);
	for (reg = 0; reg < AZF_IO_SIZE_MPU_PM / 2; reg++)
		chip->saved_regs_mpu[reg] = inw(chip->mpu_port + reg * 2);
	for (reg = 0; reg < AZF_IO_SIZE_SYNTH_PM / 2; reg++)
		chip->saved_regs_synth[reg] = inw(chip->synth_port + reg * 2);

	pci_set_power_state(pci, PCI_D3hot);
	pci_disable_device(pci);
	pci_save_state(pci);
	return 0;
}

static int
snd_azf3328_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_azf3328 *chip = card->private_data;
	int reg;

	pci_restore_state(pci);
	pci_enable_device(pci);
	pci_set_power_state(pci, PCI_D0);
	pci_set_master(pci);

	for (reg = 0; reg < AZF_IO_SIZE_IO2_PM / 2; reg++)
		outw(chip->saved_regs_io2[reg], chip->io2_port + reg * 2);
	for (reg = 0; reg < AZF_IO_SIZE_MPU_PM / 2; reg++)
		outw(chip->saved_regs_mpu[reg], chip->mpu_port + reg * 2);
	for (reg = 0; reg < AZF_IO_SIZE_SYNTH_PM / 2; reg++)
		outw(chip->saved_regs_synth[reg], chip->synth_port + reg * 2);
	for (reg = 0; reg < AZF_IO_SIZE_MIXER_PM / 2; reg++)
		outw(chip->saved_regs_mixer[reg], chip->mixer_port + reg * 2);
	for (reg = 0; reg < AZF_IO_SIZE_CODEC_PM / 2; reg++)
		outw(chip->saved_regs_codec[reg], chip->codec_port + reg * 2);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif




static struct pci_driver driver = {
	.name = "AZF3328",
	.id_table = snd_azf3328_ids,
	.probe = snd_azf3328_probe,
	.remove = __devexit_p(snd_azf3328_remove),
#ifdef CONFIG_PM
	.suspend = snd_azf3328_suspend,
	.resume = snd_azf3328_resume,
#endif
};

static int __init
alsa_card_azf3328_init(void)
{
	int err;
	snd_azf3328_dbgcallenter();
	err = pci_register_driver(&driver);
	snd_azf3328_dbgcallleave();
	return err;
}

static void __exit
alsa_card_azf3328_exit(void)
{
	snd_azf3328_dbgcallenter();
	pci_unregister_driver(&driver);
	snd_azf3328_dbgcallleave();
}

module_init(alsa_card_azf3328_init)
module_exit(alsa_card_azf3328_exit)
