/*  azt3328.c - driver for Aztech AZF3328 based soundcards (e.g. PCI168).
 *  Copyright (C) 2002, 2005 - 2011 by Andreas Mohr <andi AT lisas.de>
 *
 *  Framework borrowed from Bart Hartgers's als4000.c.
 *  Driver developed on PCI168 AP(W) version (PCI rev. 10, subsystem ID 1801),
 *  found in a Fujitsu-Siemens PC ("Cordant", aluminum case).
 *  Other versions are:
 *  PCI168 A(W), sub ID 1800
 *  PCI168 A/AP, sub ID 8000
 *  Please give me feedback in case you try my driver with one of these!!
 *
 *  Keywords: Windows XP Vista 168nt4-125.zip 168win95-125.zip PCI 168 download
 *  (XP/Vista do not support this card at all but every Linux distribution
 *   has very good support out of the box;
 *   just to make sure that the right people hit this and get to know that,
 *   despite the high level of Internet ignorance - as usual :-P -
 *   about very good support for this card - on Linux!)
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
 *  It is quite likely that the AZF3328 chip is the PCI cousin of the
 *  AZF3318 ("azt1020 pnp", "MM Pro 16") ISA chip, given very similar specs.
 *
 *  The AZF3328 chip (note: AZF3328, *not* AZT3328, that's just the driver name
 *  for compatibility reasons) from Azfin (joint-venture of Aztech and Fincitec,
 *  Fincitec acquired by National Semiconductor in 2002, together with the
 *  Fincitec-related company ARSmikro) has the following features:
 *
 *  - compatibility & compliance:
 *    - Microsoft PC 97 ("PC 97 Hardware Design Guide",
 *                       http://www.microsoft.com/whdc/archive/pcguides.mspx)
 *    - Microsoft PC 98 Baseline Audio
 *    - MPU401 UART
 *    - Sound Blaster Emulation (DOS Box)
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
 *    Well, not quite: now ac97 layer is much improved (bus-specific ops!),
 *    thus I was able to implement support - it's actually working quite well.
 *    An interesting item might be Aztech AMR 2800-W, since it's an AC97
 *    modem card which might reveal the Aztech-specific codec ID which
 *    we might want to pretend, too. Dito PCI168's brother, PCI368,
 *    where the advertising datasheet says it's AC97-based and has a
 *    Digital Enhanced Game Port.
 *  - builtin genuine OPL3 - verified to work fine, 20080506
 *  - full duplex 16bit playback/record at independent sampling rate
 *  - MPU401 (+ legacy address support, claimed by one official spec sheet)
 *    FIXME: how to enable legacy addr??
 *  - game port (legacy address support)
 *  - builtin DirectInput support, helps reduce CPU overhead (interrupt-driven
 *    features supported). - See common term "Digital Enhanced Game Port"...
 *    (probably DirectInput 3.0 spec - confirm)
 *  - builtin 3D enhancement (said to be YAMAHA Ymersion)
 *  - built-in General DirectX timer having a 20 bits counter
 *    with 1us resolution (see below!)
 *  - I2S serial output port for external DAC
 *    [FIXME: 3.3V or 5V level? maximum rate is 66.2kHz right?]
 *  - supports 33MHz PCI spec 2.1, PCI power management 1.0, compliant with ACPI
 *  - supports hardware volume control
 *  - single chip low cost solution (128 pin QFP)
 *  - supports programmable Sub-vendor and Sub-system ID [24C02 SEEPROM chip]
 *    required for Microsoft's logo compliance (FIXME: where?)
 *    At least the Trident 4D Wave DX has one bit somewhere
 *    to enable writes to PCI subsystem VID registers, that should be it.
 *    This might easily be in extended PCI reg space, since PCI168 also has
 *    some custom data starting at 0x80. What kind of config settings
 *    are located in our extended PCI space anyway??
 *  - PCI168 AP(W) card: power amplifier with 4 Watts/channel at 4 Ohms
 *    [TDA1517P chip]
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
 *  OPL3 hardware playback testing, try something like:
 *  cat /proc/asound/hwdep
 *  and
 *  aconnect -o
 *  Then use
 *  sbiload -Dhw:x,y --opl3 /usr/share/sounds/opl3/std.o3 ......./drums.o3
 *  where x,y is the xx-yy number as given in hwdep.
 *  Then try
 *  pmidi -p a:b jazz.mid
 *  where a:b is the client number plus 0 usually, as given by aconnect above.
 *  Oh, and make sure to unmute the FM mixer control (doh!)
 *  NOTE: power use during OPL3 playback is _VERY_ high (70W --> 90W!)
 *  despite no CPU activity, possibly due to hindering ACPI idling somehow.
 *  Shouldn't be a problem of the AZF3328 chip itself, I'd hope.
 *  Higher PCM / FM mixer levels seem to conflict (causes crackling),
 *  at least sometimes.   Maybe even use with hardware sequencer timer above :)
 *  adplay/adplug-utils might soon offer hardware-based OPL3 playback, too.
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
 *  - use speaker (amplifier) output instead of headphone output
 *    (in case crackling is due to overloaded output clipping)
 *  - plug card into a different PCI slot, preferably one that isn't shared
 *    too much (this helps a lot, but not completely!)
 *  - get rid of PCI VGA card, use AGP instead
 *  - upgrade or downgrade BIOS
 *  - fiddle with PCI latency settings (setpci -v -s BUSID latency_timer=XX)
 *    Not too helpful.
 *  - Disable ACPI/power management/"Auto Detect RAM/PCI Clk" in BIOS
 *
 * BUGS
 *  - full-duplex might *still* be problematic, however a recent test was fine
 *  - (non-bug) "Bass/Treble or 3D settings don't work" - they do get evaluated
 *    if you set PCM output switch to "pre 3D" instead of "post 3D".
 *    If this can't be set, then get a mixer application that Isn't Stupid (tm)
 *    (e.g. kmix, gamix) - unfortunately several are!!
 *  - locking is not entirely clean, especially the audio stream activity
 *    ints --> may be racy
 *  - an _unconnected_ secondary joystick at the gameport will be reported
 *    to be "active" (floating values, not precisely -1) due to the way we need
 *    to read the Digital Enhanced Game Port. Not sure whether it is fixable.
 *
 * TODO
 *  - use PCI_VDEVICE
 *  - verify driver status on x86_64
 *  - test multi-card driver operation
 *  - (ab)use 1MHz DirectX timer as kernel clocksource
 *  - test MPU401 MIDI playback etc.
 *  - add more power micro-management (disable various units of the card
 *    as long as they're unused, to improve audio quality and save power).
 *    However this requires more I/O ports which I haven't figured out yet
 *    and which thus might not even exist...
 *    The standard suspend/resume functionality could probably make use of
 *    some improvement, too...
 *  - figure out what all unknown port bits are responsible for
 *  - figure out some cleverly evil scheme to possibly make ALSA AC97 code
 *    fully accept our quite incompatible ""AC97"" mixer and thus save some
 *    code (but I'm not too optimistic that doing this is possible at all)
 *  - use MMIO (memory-mapped I/O)? Slightly faster access, e.g. for gameport.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/bug.h> /* WARN_ONCE */
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/initval.h>
/*
 * Config switch, to use ALSA's AC97 layer instead of old custom mixer crap.
 * If the AC97 compatibility parts we needed to implement locally turn out
 * to work nicely, then remove the old implementation eventually.
 */
#define AZF_USE_AC97_LAYER 1

#ifdef AZF_USE_AC97_LAYER
#include <sound/ac97_codec.h>
#endif
#include "azt3328.h"

MODULE_AUTHOR("Andreas Mohr <andi AT lisas.de>");
MODULE_DESCRIPTION("Aztech AZF3328 (PCI168)");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Aztech,AZF3328}}");

#if IS_REACHABLE(CONFIG_GAMEPORT)
#define SUPPORT_GAMEPORT 1
#endif

/* === Debug settings ===
  Further diagnostic functionality than the settings below
  does not need to be provided, since one can easily write a POSIX shell script
  to dump the card's I/O ports (those listed in lspci -v -v):
  dump()
  {
    local descr=$1; local addr=$2; local count=$3

    echo "${descr}: ${count} @ ${addr}:"
    dd if=/dev/port skip=`printf %d ${addr}` count=${count} bs=1 \
      2>/dev/null| hexdump -C
  }
  and then use something like
  "dump joy200 0x200 8", "dump mpu388 0x388 4", "dump joy 0xb400 8",
  "dump codec00 0xa800 32", "dump mixer 0xb800 64", "dump synth 0xbc00 8",
  possibly within a "while true; do ... sleep 1; done" loop.
  Tweaking ports could be done using
  VALSTRING="`printf "%02x" $value`"
  printf "\x""$VALSTRING"|dd of=/dev/port seek=`printf %d ${addr}` bs=1 \
    2>/dev/null
*/

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for AZF3328 soundcard.");

static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for AZF3328 soundcard.");

static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable AZF3328 soundcard.");

static int seqtimer_scaling = 128;
module_param(seqtimer_scaling, int, 0444);
MODULE_PARM_DESC(seqtimer_scaling, "Set 1024000Hz sequencer timer scale factor (lockup danger!). Default 128.");

enum snd_azf3328_codec_type {
  /* warning: fixed indices (also used for bitmask checks!) */
  AZF_CODEC_PLAYBACK = 0,
  AZF_CODEC_CAPTURE = 1,
  AZF_CODEC_I2S_OUT = 2,
};

struct snd_azf3328_codec_data {
	unsigned long io_base; /* keep first! (avoid offset calc) */
	unsigned int dma_base; /* helper to avoid an indirection in hotpath */
	spinlock_t *lock; /* TODO: convert to our own per-codec lock member */
	struct snd_pcm_substream *substream;
	bool running;
	enum snd_azf3328_codec_type type;
	const char *name;
};

struct snd_azf3328 {
	/* often-used fields towards beginning, then grouped */

	unsigned long ctrl_io; /* usually 0xb000, size 128 */
	unsigned long game_io;  /* usually 0xb400, size 8 */
	unsigned long mpu_io;   /* usually 0xb800, size 4 */
	unsigned long opl3_io; /* usually 0xbc00, size 8 */
	unsigned long mixer_io; /* usually 0xc000, size 64 */

	spinlock_t reg_lock;

	struct snd_timer *timer;

	struct snd_pcm *pcm[3];

	/* playback, recording and I2S out codecs */
	struct snd_azf3328_codec_data codecs[3];

#ifdef AZF_USE_AC97_LAYER
	struct snd_ac97 *ac97;
#endif

	struct snd_card *card;
	struct snd_rawmidi *rmidi;

#ifdef SUPPORT_GAMEPORT
	struct gameport *gameport;
	u16 axes[4];
#endif

	struct pci_dev *pci;
	int irq;

	/* register 0x6a is write-only, thus need to remember setting.
	 * If we need to add more registers here, then we might try to fold this
	 * into some transparent combined shadow register handling with
	 * CONFIG_PM register storage below, but that's slightly difficult. */
	u16 shadow_reg_ctrl_6AH;

#ifdef CONFIG_PM_SLEEP
	/* register value containers for power management
	 * Note: not always full I/O range preserved (similar to Win driver!) */
	u32 saved_regs_ctrl[AZF_ALIGN(AZF_IO_SIZE_CTRL_PM) / 4];
	u32 saved_regs_game[AZF_ALIGN(AZF_IO_SIZE_GAME_PM) / 4];
	u32 saved_regs_mpu[AZF_ALIGN(AZF_IO_SIZE_MPU_PM) / 4];
	u32 saved_regs_opl3[AZF_ALIGN(AZF_IO_SIZE_OPL3_PM) / 4];
	u32 saved_regs_mixer[AZF_ALIGN(AZF_IO_SIZE_MIXER_PM) / 4];
#endif
};

static const struct pci_device_id snd_azf3328_ids[] = {
	{ 0x122D, 0x50DC, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },   /* PCI168/3328 */
	{ 0x122D, 0x80DA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },   /* 3328 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_azf3328_ids);


static int
snd_azf3328_io_reg_setb(unsigned reg, u8 mask, bool do_set)
{
	/* Well, strictly spoken, the inb/outb sequence isn't atomic
	   and would need locking. However we currently don't care
	   since it potentially complicates matters. */
	u8 prev = inb(reg), new;

	new = (do_set) ? (prev|mask) : (prev & ~mask);
	/* we need to always write the new value no matter whether it differs
	 * or not, since some register bits don't indicate their setting */
	outb(new, reg);
	if (new != prev)
		return 1;

	return 0;
}

static inline void
snd_azf3328_codec_outb(const struct snd_azf3328_codec_data *codec,
		       unsigned reg,
		       u8 value
)
{
	outb(value, codec->io_base + reg);
}

static inline u8
snd_azf3328_codec_inb(const struct snd_azf3328_codec_data *codec, unsigned reg)
{
	return inb(codec->io_base + reg);
}

static inline void
snd_azf3328_codec_outw(const struct snd_azf3328_codec_data *codec,
		       unsigned reg,
		       u16 value
)
{
	outw(value, codec->io_base + reg);
}

static inline u16
snd_azf3328_codec_inw(const struct snd_azf3328_codec_data *codec, unsigned reg)
{
	return inw(codec->io_base + reg);
}

static inline void
snd_azf3328_codec_outl(const struct snd_azf3328_codec_data *codec,
		       unsigned reg,
		       u32 value
)
{
	outl(value, codec->io_base + reg);
}

static inline void
snd_azf3328_codec_outl_multi(const struct snd_azf3328_codec_data *codec,
			     unsigned reg, const void *buffer, int count
)
{
	unsigned long addr = codec->io_base + reg;
	if (count) {
		const u32 *buf = buffer;
		do {
			outl(*buf++, addr);
			addr += 4;
		} while (--count);
	}
}

static inline u32
snd_azf3328_codec_inl(const struct snd_azf3328_codec_data *codec, unsigned reg)
{
	return inl(codec->io_base + reg);
}

static inline void
snd_azf3328_ctrl_outb(const struct snd_azf3328 *chip, unsigned reg, u8 value)
{
	outb(value, chip->ctrl_io + reg);
}

static inline u8
snd_azf3328_ctrl_inb(const struct snd_azf3328 *chip, unsigned reg)
{
	return inb(chip->ctrl_io + reg);
}

static inline u16
snd_azf3328_ctrl_inw(const struct snd_azf3328 *chip, unsigned reg)
{
	return inw(chip->ctrl_io + reg);
}

static inline void
snd_azf3328_ctrl_outw(const struct snd_azf3328 *chip, unsigned reg, u16 value)
{
	outw(value, chip->ctrl_io + reg);
}

static inline void
snd_azf3328_ctrl_outl(const struct snd_azf3328 *chip, unsigned reg, u32 value)
{
	outl(value, chip->ctrl_io + reg);
}

static inline void
snd_azf3328_game_outb(const struct snd_azf3328 *chip, unsigned reg, u8 value)
{
	outb(value, chip->game_io + reg);
}

static inline void
snd_azf3328_game_outw(const struct snd_azf3328 *chip, unsigned reg, u16 value)
{
	outw(value, chip->game_io + reg);
}

static inline u8
snd_azf3328_game_inb(const struct snd_azf3328 *chip, unsigned reg)
{
	return inb(chip->game_io + reg);
}

static inline u16
snd_azf3328_game_inw(const struct snd_azf3328 *chip, unsigned reg)
{
	return inw(chip->game_io + reg);
}

static inline void
snd_azf3328_mixer_outw(const struct snd_azf3328 *chip, unsigned reg, u16 value)
{
	outw(value, chip->mixer_io + reg);
}

static inline u16
snd_azf3328_mixer_inw(const struct snd_azf3328 *chip, unsigned reg)
{
	return inw(chip->mixer_io + reg);
}

#define AZF_MUTE_BIT 0x80

static bool
snd_azf3328_mixer_mute_control(const struct snd_azf3328 *chip,
			   unsigned reg, bool do_mute
)
{
	unsigned long portbase = chip->mixer_io + reg + 1;
	bool updated;

	/* the mute bit is on the *second* (i.e. right) register of a
	 * left/right channel setting */
	updated = snd_azf3328_io_reg_setb(portbase, AZF_MUTE_BIT, do_mute);

	/* indicate whether it was muted before */
	return (do_mute) ? !updated : updated;
}

static inline bool
snd_azf3328_mixer_mute_control_master(const struct snd_azf3328 *chip,
			   bool do_mute
)
{
	return snd_azf3328_mixer_mute_control(
		chip,
		IDX_MIXER_PLAY_MASTER,
		do_mute
	);
}

static inline bool
snd_azf3328_mixer_mute_control_pcm(const struct snd_azf3328 *chip,
			   bool do_mute
)
{
	return snd_azf3328_mixer_mute_control(
		chip,
		IDX_MIXER_WAVEOUT,
		do_mute
	);
}

static inline void
snd_azf3328_mixer_reset(const struct snd_azf3328 *chip)
{
	/* reset (close) mixer:
	 * first mute master volume, then reset
	 */
	snd_azf3328_mixer_mute_control_master(chip, 1);
	snd_azf3328_mixer_outw(chip, IDX_MIXER_RESET, 0x0000);
}

#ifdef AZF_USE_AC97_LAYER

static inline void
snd_azf3328_mixer_ac97_map_unsupported(const struct snd_azf3328 *chip,
				       unsigned short reg, const char *mode)
{
	/* need to add some more or less clever emulation? */
	dev_warn(chip->card->dev,
		"missing %s emulation for AC97 register 0x%02x!\n",
		mode, reg);
}

/*
 * Need to have _special_ AC97 mixer hardware register index mapper,
 * to compensate for the issue of a rather AC97-incompatible hardware layout.
 */
#define AZF_REG_MASK 0x3f
#define AZF_AC97_REG_UNSUPPORTED 0x8000
#define AZF_AC97_REG_REAL_IO_READ 0x4000
#define AZF_AC97_REG_REAL_IO_WRITE 0x2000
#define AZF_AC97_REG_REAL_IO_RW \
	(AZF_AC97_REG_REAL_IO_READ | AZF_AC97_REG_REAL_IO_WRITE)
#define AZF_AC97_REG_EMU_IO_READ 0x0400
#define AZF_AC97_REG_EMU_IO_WRITE 0x0200
#define AZF_AC97_REG_EMU_IO_RW \
	(AZF_AC97_REG_EMU_IO_READ | AZF_AC97_REG_EMU_IO_WRITE)
static unsigned short
snd_azf3328_mixer_ac97_map_reg_idx(unsigned short reg)
{
	static const struct {
		unsigned short azf_reg;
	} azf_reg_mapper[] = {
		/* Especially when taking into consideration
		 * mono/stereo-based sequence of azf vs. AC97 control series,
		 * it's quite obvious that azf simply got rid
		 * of the AC97_HEADPHONE control at its intended offset,
		 * thus shifted _all_ controls by one,
		 * and _then_ simply added it as an FMSYNTH control at the end,
		 * to make up for the offset.
		 * This means we'll have to translate indices here as
		 * needed and then do some tiny AC97 patch action
		 * (snd_ac97_rename_vol_ctl() etc.) - that's it.
		 */
		{ /* AC97_RESET */ IDX_MIXER_RESET
			| AZF_AC97_REG_REAL_IO_WRITE
			| AZF_AC97_REG_EMU_IO_READ },
		{ /* AC97_MASTER */ IDX_MIXER_PLAY_MASTER },
		 /* note large shift: AC97_HEADPHONE to IDX_MIXER_FMSYNTH! */
		{ /* AC97_HEADPHONE */ IDX_MIXER_FMSYNTH },
		{ /* AC97_MASTER_MONO */ IDX_MIXER_MODEMOUT },
		{ /* AC97_MASTER_TONE */ IDX_MIXER_BASSTREBLE },
		{ /* AC97_PC_BEEP */ IDX_MIXER_PCBEEP },
		{ /* AC97_PHONE */ IDX_MIXER_MODEMIN },
		{ /* AC97_MIC */ IDX_MIXER_MIC },
		{ /* AC97_LINE */ IDX_MIXER_LINEIN },
		{ /* AC97_CD */ IDX_MIXER_CDAUDIO },
		{ /* AC97_VIDEO */ IDX_MIXER_VIDEO },
		{ /* AC97_AUX */ IDX_MIXER_AUX },
		{ /* AC97_PCM */ IDX_MIXER_WAVEOUT },
		{ /* AC97_REC_SEL */ IDX_MIXER_REC_SELECT },
		{ /* AC97_REC_GAIN */ IDX_MIXER_REC_VOLUME },
		{ /* AC97_REC_GAIN_MIC */ AZF_AC97_REG_EMU_IO_RW },
		{ /* AC97_GENERAL_PURPOSE */ IDX_MIXER_ADVCTL2 },
		{ /* AC97_3D_CONTROL */ IDX_MIXER_ADVCTL1 },
	};

	unsigned short reg_azf = AZF_AC97_REG_UNSUPPORTED;

	/* azf3328 supports the low-numbered and low-spec:ed range
	   of AC97 regs only */
	if (reg <= AC97_3D_CONTROL) {
		unsigned short reg_idx = reg / 2;
		reg_azf = azf_reg_mapper[reg_idx].azf_reg;
		/* a translation-only entry means it's real read/write: */
		if (!(reg_azf & ~AZF_REG_MASK))
			reg_azf |= AZF_AC97_REG_REAL_IO_RW;
	} else {
		switch (reg) {
		case AC97_POWERDOWN:
			reg_azf = AZF_AC97_REG_EMU_IO_RW;
			break;
		case AC97_EXTENDED_ID:
			reg_azf = AZF_AC97_REG_EMU_IO_READ;
			break;
		case AC97_EXTENDED_STATUS:
			/* I don't know what the h*ll AC97 layer
			 * would consult this _extended_ register for
			 * given a base-AC97-advertised card,
			 * but let's just emulate it anyway :-P
			 */
			reg_azf = AZF_AC97_REG_EMU_IO_RW;
			break;
		case AC97_VENDOR_ID1:
		case AC97_VENDOR_ID2:
			reg_azf = AZF_AC97_REG_EMU_IO_READ;
			break;
		}
	}
	return reg_azf;
}

static const unsigned short
azf_emulated_ac97_caps =
	AC97_BC_DEDICATED_MIC |
	AC97_BC_BASS_TREBLE |
	/* Headphone is an FM Synth control here */
	AC97_BC_HEADPHONE |
	/* no AC97_BC_LOUDNESS! */
	/* mask 0x7c00 is
	   vendor-specific 3D enhancement
	   vendor indicator.
	   Since there actually _is_ an
	   entry for Aztech Labs
	   (13), make damn sure
	   to indicate it. */
	(13 << 10);

static const unsigned short
azf_emulated_ac97_powerdown =
	/* pretend everything to be active */
		AC97_PD_ADC_STATUS |
		AC97_PD_DAC_STATUS |
		AC97_PD_MIXER_STATUS |
		AC97_PD_VREF_STATUS;

/*
 * Emulated, _inofficial_ vendor ID
 * (there might be some devices such as the MR 2800-W
 * which could reveal the real Aztech AC97 ID).
 * We choose to use "AZT" prefix, and then use 1 to indicate PCI168
 * (better don't use 0x68 since there's a PCI368 as well).
 */
static const unsigned int
azf_emulated_ac97_vendor_id = 0x415a5401;

static unsigned short
snd_azf3328_mixer_ac97_read(struct snd_ac97 *ac97, unsigned short reg_ac97)
{
	const struct snd_azf3328 *chip = ac97->private_data;
	unsigned short reg_azf = snd_azf3328_mixer_ac97_map_reg_idx(reg_ac97);
	unsigned short reg_val = 0;
	bool unsupported = false;

	dev_dbg(chip->card->dev, "snd_azf3328_mixer_ac97_read reg_ac97 %u\n",
		reg_ac97);
	if (reg_azf & AZF_AC97_REG_UNSUPPORTED)
		unsupported = true;
	else {
		if (reg_azf & AZF_AC97_REG_REAL_IO_READ)
			reg_val = snd_azf3328_mixer_inw(chip,
						reg_azf & AZF_REG_MASK);
		else {
			/*
			 * Proceed with dummy I/O read,
			 * to ensure compatible timing where this may matter.
			 * (ALSA AC97 layer usually doesn't call I/O functions
			 * due to intelligent I/O caching anyway)
			 * Choose a mixer register that's thoroughly unrelated
			 * to common audio (try to minimize distortion).
			 */
			snd_azf3328_mixer_inw(chip, IDX_MIXER_SOMETHING30H);
		}

		if (reg_azf & AZF_AC97_REG_EMU_IO_READ) {
			switch (reg_ac97) {
			case AC97_RESET:
				reg_val |= azf_emulated_ac97_caps;
				break;
			case AC97_POWERDOWN:
				reg_val |= azf_emulated_ac97_powerdown;
				break;
			case AC97_EXTENDED_ID:
			case AC97_EXTENDED_STATUS:
				/* AFAICS we simply can't support anything: */
				reg_val |= 0;
				break;
			case AC97_VENDOR_ID1:
				reg_val = azf_emulated_ac97_vendor_id >> 16;
				break;
			case AC97_VENDOR_ID2:
				reg_val = azf_emulated_ac97_vendor_id & 0xffff;
				break;
			default:
				unsupported = true;
				break;
			}
		}
	}
	if (unsupported)
		snd_azf3328_mixer_ac97_map_unsupported(chip, reg_ac97, "read");

	return reg_val;
}

static void
snd_azf3328_mixer_ac97_write(struct snd_ac97 *ac97,
		     unsigned short reg_ac97, unsigned short val)
{
	const struct snd_azf3328 *chip = ac97->private_data;
	unsigned short reg_azf = snd_azf3328_mixer_ac97_map_reg_idx(reg_ac97);
	bool unsupported = false;

	dev_dbg(chip->card->dev,
		"snd_azf3328_mixer_ac97_write reg_ac97 %u val %u\n",
		reg_ac97, val);
	if (reg_azf & AZF_AC97_REG_UNSUPPORTED)
		unsupported = true;
	else {
		if (reg_azf & AZF_AC97_REG_REAL_IO_WRITE)
			snd_azf3328_mixer_outw(
				chip,
				reg_azf & AZF_REG_MASK,
				val
			);
		else
		if (reg_azf & AZF_AC97_REG_EMU_IO_WRITE) {
			switch (reg_ac97) {
			case AC97_REC_GAIN_MIC:
			case AC97_POWERDOWN:
			case AC97_EXTENDED_STATUS:
				/*
				 * Silently swallow these writes.
				 * Since for most registers our card doesn't
				 * actually support a comparable feature,
				 * this is exactly what we should do here.
				 * The AC97 layer's I/O caching probably
				 * automatically takes care of all the rest...
				 * (remembers written values etc.)
				 */
				break;
			default:
				unsupported = true;
				break;
			}
		}
	}
	if (unsupported)
		snd_azf3328_mixer_ac97_map_unsupported(chip, reg_ac97, "write");
}

static int
snd_azf3328_mixer_new(struct snd_azf3328 *chip)
{
	struct snd_ac97_bus *bus;
	struct snd_ac97_template ac97;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_azf3328_mixer_ac97_write,
		.read = snd_azf3328_mixer_ac97_read,
	};
	int rc;

	memset(&ac97, 0, sizeof(ac97));
	ac97.scaps = AC97_SCAP_SKIP_MODEM
			| AC97_SCAP_AUDIO /* we support audio! */
			| AC97_SCAP_NO_SPDIF;
	ac97.private_data = chip;
	ac97.pci = chip->pci;

	/*
	 * ALSA's AC97 layer has terrible init crackling issues,
	 * unfortunately, and since it makes use of AC97_RESET,
	 * there's no use trying to mute Master Playback proactively.
	 */

	rc = snd_ac97_bus(chip->card, 0, &ops, NULL, &bus);
	if (!rc)
		rc = snd_ac97_mixer(bus, &ac97, &chip->ac97);
		/*
		 * Make sure to complain loudly in case of AC97 init failure,
		 * since failure may happen quite often,
		 * due to this card being a very quirky AC97 "lookalike".
		 */
	if (rc)
		dev_err(chip->card->dev, "AC97 init failed, err %d!\n", rc);

	/* If we return an error here, then snd_card_free() should
	 * free up any ac97 codecs that got created, as well as the bus.
	 */
	return rc;
}
#else /* AZF_USE_AC97_LAYER */
static void
snd_azf3328_mixer_write_volume_gradually(const struct snd_azf3328 *chip,
					 unsigned reg,
					 unsigned char dst_vol_left,
					 unsigned char dst_vol_right,
					 int chan_sel, int delay
)
{
	unsigned long portbase = chip->mixer_io + reg;
	unsigned char curr_vol_left = 0, curr_vol_right = 0;
	int left_change = 0, right_change = 0;

	if (chan_sel & SET_CHAN_LEFT) {
		curr_vol_left  = inb(portbase + 1);

		/* take care of muting flag contained in left channel */
		if (curr_vol_left & AZF_MUTE_BIT)
			dst_vol_left |= AZF_MUTE_BIT;
		else
			dst_vol_left &= ~AZF_MUTE_BIT;

		left_change = (curr_vol_left > dst_vol_left) ? -1 : 1;
	}

	if (chan_sel & SET_CHAN_RIGHT) {
		curr_vol_right = inb(portbase + 0);

		right_change = (curr_vol_right > dst_vol_right) ? -1 : 1;
	}

	do {
		if (left_change) {
			if (curr_vol_left != dst_vol_left) {
				curr_vol_left += left_change;
				outb(curr_vol_left, portbase + 1);
			} else
			    left_change = 0;
		}
		if (right_change) {
			if (curr_vol_right != dst_vol_right) {
				curr_vol_right += right_change;

			/* during volume change, the right channel is crackling
			 * somewhat more than the left channel, unfortunately.
			 * This seems to be a hardware issue. */
				outb(curr_vol_right, portbase + 0);
			} else
			    right_change = 0;
		}
		if (delay)
			mdelay(delay);
	} while ((left_change) || (right_change));
}

/*
 * general mixer element
 */
struct azf3328_mixer_reg {
	unsigned reg;
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

	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	uinfo->type = reg.mask == 1 ?
		SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = reg.stereo + 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = reg.mask;
	return 0;
}

static int
snd_azf3328_get_mixer(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_azf3328 *chip = snd_kcontrol_chip(kcontrol);
	struct azf3328_mixer_reg reg;
	u16 oreg, val;

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
	dev_dbg(chip->card->dev,
		"get: %02x is %04x -> vol %02lx|%02lx (shift %02d|%02d, mask %02x, inv. %d, stereo %d)\n",
		reg.reg, oreg,
		ucontrol->value.integer.value[0], ucontrol->value.integer.value[1],
		reg.lchan_shift, reg.rchan_shift, reg.mask, reg.invert, reg.stereo);
	return 0;
}

static int
snd_azf3328_put_mixer(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_azf3328 *chip = snd_kcontrol_chip(kcontrol);
	struct azf3328_mixer_reg reg;
	u16 oreg, nreg, val;

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

	dev_dbg(chip->card->dev,
		"put: %02x to %02lx|%02lx, oreg %04x; shift %02d|%02d -> nreg %04x; after: %04x\n",
		reg.reg, ucontrol->value.integer.value[0], ucontrol->value.integer.value[1],
		oreg, reg.lchan_shift, reg.rchan_shift,
		nreg, snd_azf3328_mixer_inw(chip, reg.reg));
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
	const char * const *p = NULL;

	snd_azf3328_mixer_reg_decode(&reg, kcontrol->private_value);
	if (reg.reg == IDX_MIXER_ADVCTL2) {
		switch(reg.lchan_shift) {
		case 8: /* modem out sel */
			p = texts1;
			break;
		case 9: /* mono sel source */
			p = texts2;
			break;
		case 15: /* PCM Out Path */
			p = texts4;
			break;
		}
	} else if (reg.reg == IDX_MIXER_REC_SELECT)
		p = texts3;

	return snd_ctl_enum_info(uinfo,
				 (reg.reg == IDX_MIXER_REC_SELECT) ? 2 : 1,
				 reg.enum_c, p);
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

	dev_dbg(chip->card->dev,
		"get_enum: %02x is %04x -> %d|%d (shift %02d, enum_c %d)\n",
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
	u16 oreg, nreg, val;

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

	dev_dbg(chip->card->dev,
		"put_enum: %02x to %04x, oreg %04x\n", reg.reg, val, oreg);
	return (nreg != oreg);
}

static struct snd_kcontrol_new snd_azf3328_mixer_controls[] = {
	AZF3328_MIXER_SWITCH("Master Playback Switch", IDX_MIXER_PLAY_MASTER, 15, 1),
	AZF3328_MIXER_VOL_STEREO("Master Playback Volume", IDX_MIXER_PLAY_MASTER, 0x1f, 1),
	AZF3328_MIXER_SWITCH("PCM Playback Switch", IDX_MIXER_WAVEOUT, 15, 1),
	AZF3328_MIXER_VOL_STEREO("PCM Playback Volume",
					IDX_MIXER_WAVEOUT, 0x1f, 1),
	AZF3328_MIXER_SWITCH("PCM 3D Bypass Playback Switch",
					IDX_MIXER_ADVCTL2, 7, 1),
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
	AZF3328_MIXER_SWITCH("Beep Playback Switch", IDX_MIXER_PCBEEP, 15, 1),
	AZF3328_MIXER_VOL_SPECIAL("Beep Playback Volume", IDX_MIXER_PCBEEP, 0x0f, 1, 1),
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
	AZF3328_MIXER_ENUM("PCM Output Route", IDX_MIXER_ADVCTL2, 2, 15), /* PCM Out Path, place in front since it controls *both* 3D and Bass/Treble! */
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

static u16 snd_azf3328_init_values[][2] = {
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

static int
snd_azf3328_mixer_new(struct snd_azf3328 *chip)
{
	struct snd_card *card;
	const struct snd_kcontrol_new *sw;
	unsigned int idx;
	int err;

	if (snd_BUG_ON(!chip || !chip->card))
		return -EINVAL;

	card = chip->card;

	/* mixer reset */
	snd_azf3328_mixer_outw(chip, IDX_MIXER_RESET, 0x0000);

	/* mute and zero volume channels */
	for (idx = 0; idx < ARRAY_SIZE(snd_azf3328_init_values); ++idx) {
		snd_azf3328_mixer_outw(chip,
			snd_azf3328_init_values[idx][0],
			snd_azf3328_init_values[idx][1]);
	}

	/* add mixer controls */
	sw = snd_azf3328_mixer_controls;
	for (idx = 0; idx < ARRAY_SIZE(snd_azf3328_mixer_controls);
			++idx, ++sw) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(sw, chip))) < 0)
			return err;
	}
	snd_component_add(card, "AZF3328 mixer");
	strcpy(card->mixername, "AZF3328 mixer");

	return 0;
}
#endif /* AZF_USE_AC97_LAYER */

static int
snd_azf3328_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int
snd_azf3328_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static void
snd_azf3328_codec_setfmt(struct snd_azf3328_codec_data *codec,
			       enum azf_freq_t bitrate,
			       unsigned int format_width,
			       unsigned int channels
)
{
	unsigned long flags;
	u16 val = 0xff00;
	u8 freq = 0;

	switch (bitrate) {
	case AZF_FREQ_4000:  freq = SOUNDFORMAT_FREQ_SUSPECTED_4000; break;
	case AZF_FREQ_4800:  freq = SOUNDFORMAT_FREQ_SUSPECTED_4800; break;
	case AZF_FREQ_5512:
		/* the AZF3328 names it "5510" for some strange reason */
			     freq = SOUNDFORMAT_FREQ_5510; break;
	case AZF_FREQ_6620:  freq = SOUNDFORMAT_FREQ_6620; break;
	case AZF_FREQ_8000:  freq = SOUNDFORMAT_FREQ_8000; break;
	case AZF_FREQ_9600:  freq = SOUNDFORMAT_FREQ_9600; break;
	case AZF_FREQ_11025: freq = SOUNDFORMAT_FREQ_11025; break;
	case AZF_FREQ_13240: freq = SOUNDFORMAT_FREQ_SUSPECTED_13240; break;
	case AZF_FREQ_16000: freq = SOUNDFORMAT_FREQ_16000; break;
	case AZF_FREQ_22050: freq = SOUNDFORMAT_FREQ_22050; break;
	case AZF_FREQ_32000: freq = SOUNDFORMAT_FREQ_32000; break;
	default:
		snd_printk(KERN_WARNING "unknown bitrate %d, assuming 44.1kHz!\n", bitrate);
		/* fall-through */
	case AZF_FREQ_44100: freq = SOUNDFORMAT_FREQ_44100; break;
	case AZF_FREQ_48000: freq = SOUNDFORMAT_FREQ_48000; break;
	case AZF_FREQ_66200: freq = SOUNDFORMAT_FREQ_SUSPECTED_66200; break;
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

	val |= freq;

	if (channels == 2)
		val |= SOUNDFORMAT_FLAG_2CHANNELS;

	if (format_width == 16)
		val |= SOUNDFORMAT_FLAG_16BIT;

	spin_lock_irqsave(codec->lock, flags);

	/* set bitrate/format */
	snd_azf3328_codec_outw(codec, IDX_IO_CODEC_SOUNDFORMAT, val);

	/* changing the bitrate/format settings switches off the
	 * audio output with an annoying click in case of 8/16bit format change
	 * (maybe shutting down DAC/ADC?), thus immediately
	 * do some tweaking to reenable it and get rid of the clicking
	 * (FIXME: yes, it works, but what exactly am I doing here?? :)
	 * FIXME: does this have some side effects for full-duplex
	 * or other dramatic side effects? */
	/* do it for non-capture codecs only */
	if (codec->type != AZF_CODEC_CAPTURE)
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS,
			snd_azf3328_codec_inw(codec, IDX_IO_CODEC_DMA_FLAGS) |
			DMA_RUN_SOMETHING1 |
			DMA_RUN_SOMETHING2 |
			SOMETHING_ALMOST_ALWAYS_SET |
			DMA_EPILOGUE_SOMETHING |
			DMA_SOMETHING_ELSE
		);

	spin_unlock_irqrestore(codec->lock, flags);
}

static inline void
snd_azf3328_codec_setfmt_lowpower(struct snd_azf3328_codec_data *codec
)
{
	/* choose lowest frequency for low power consumption.
	 * While this will cause louder noise due to rather coarse frequency,
	 * it should never matter since output should always
	 * get disabled properly when idle anyway. */
	snd_azf3328_codec_setfmt(codec, AZF_FREQ_4000, 8, 1);
}

static void
snd_azf3328_ctrl_reg_6AH_update(struct snd_azf3328 *chip,
					unsigned bitmask,
					bool enable
)
{
	bool do_mask = !enable;
	if (do_mask)
		chip->shadow_reg_ctrl_6AH |= bitmask;
	else
		chip->shadow_reg_ctrl_6AH &= ~bitmask;
	dev_dbg(chip->card->dev,
		"6AH_update mask 0x%04x do_mask %d: val 0x%04x\n",
		bitmask, do_mask, chip->shadow_reg_ctrl_6AH);
	snd_azf3328_ctrl_outw(chip, IDX_IO_6AH, chip->shadow_reg_ctrl_6AH);
}

static inline void
snd_azf3328_ctrl_enable_codecs(struct snd_azf3328 *chip, bool enable)
{
	dev_dbg(chip->card->dev, "codec_enable %d\n", enable);
	/* no idea what exactly is being done here, but I strongly assume it's
	 * PM related */
	snd_azf3328_ctrl_reg_6AH_update(
		chip, IO_6A_PAUSE_PLAYBACK_BIT8, enable
	);
}

static void
snd_azf3328_ctrl_codec_activity(struct snd_azf3328 *chip,
				enum snd_azf3328_codec_type codec_type,
				bool enable
)
{
	struct snd_azf3328_codec_data *codec = &chip->codecs[codec_type];
	bool need_change = (codec->running != enable);

	dev_dbg(chip->card->dev,
		"codec_activity: %s codec, enable %d, need_change %d\n",
				codec->name, enable, need_change
	);
	if (need_change) {
		static const struct {
			enum snd_azf3328_codec_type other1;
			enum snd_azf3328_codec_type other2;
		} peer_codecs[3] =
			{ { AZF_CODEC_CAPTURE, AZF_CODEC_I2S_OUT },
			  { AZF_CODEC_PLAYBACK, AZF_CODEC_I2S_OUT },
			  { AZF_CODEC_PLAYBACK, AZF_CODEC_CAPTURE } };
		bool call_function;

		if (enable)
			/* if enable codec, call enable_codecs func
			   to enable codec supply... */
			call_function = 1;
		else {
			/* ...otherwise call enable_codecs func
			   (which globally shuts down operation of codecs)
			   only in case the other codecs are currently
			   not active either! */
			call_function =
				((!chip->codecs[peer_codecs[codec_type].other1]
					.running)
			     &&  (!chip->codecs[peer_codecs[codec_type].other2]
					.running));
		}
		if (call_function)
			snd_azf3328_ctrl_enable_codecs(chip, enable);

		/* ...and adjust clock, too
		 * (reduce noise and power consumption) */
		if (!enable)
			snd_azf3328_codec_setfmt_lowpower(codec);
		codec->running = enable;
	}
}

static void
snd_azf3328_codec_setdmaa(struct snd_azf3328 *chip,
			  struct snd_azf3328_codec_data *codec,
			  unsigned long addr,
			  unsigned int period_bytes,
			  unsigned int buffer_bytes
)
{
	WARN_ONCE(period_bytes & 1, "odd period length!?\n");
	WARN_ONCE(buffer_bytes != 2 * period_bytes,
		 "missed our input expectations! %u vs. %u\n",
		 buffer_bytes, period_bytes);
	if (!codec->running) {
		/* AZF3328 uses a two buffer pointer DMA transfer approach */

		unsigned long flags;

		/* width 32bit (prevent overflow): */
		u32 area_length;
		struct codec_setup_io {
			u32 dma_start_1;
			u32 dma_start_2;
			u32 dma_lengths;
		} __attribute__((packed)) setup_io;

		area_length = buffer_bytes/2;

		setup_io.dma_start_1 = addr;
		setup_io.dma_start_2 = addr+area_length;

		dev_dbg(chip->card->dev,
			"setdma: buffers %08x[%u] / %08x[%u], %u, %u\n",
				setup_io.dma_start_1, area_length,
				setup_io.dma_start_2, area_length,
				period_bytes, buffer_bytes);

		/* Hmm, are we really supposed to decrement this by 1??
		   Most definitely certainly not: configuring full length does
		   work properly (i.e. likely better), and BTW we
		   violated possibly differing frame sizes with this...

		area_length--; |* max. index *|
		*/

		/* build combined I/O buffer length word */
		setup_io.dma_lengths = (area_length << 16) | (area_length);

		spin_lock_irqsave(codec->lock, flags);
		snd_azf3328_codec_outl_multi(
			codec, IDX_IO_CODEC_DMA_START_1, &setup_io, 3
		);
		spin_unlock_irqrestore(codec->lock, flags);
	}
}

static int
snd_azf3328_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_azf3328_codec_data *codec = runtime->private_data;
#if 0
        unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);
#endif

	codec->dma_base = runtime->dma_addr;

#if 0
	snd_azf3328_codec_setfmt(codec,
		runtime->rate,
		snd_pcm_format_width(runtime->format),
		runtime->channels);
	snd_azf3328_codec_setdmaa(chip, codec,
					runtime->dma_addr, count, size);
#endif
	return 0;
}

static int
snd_azf3328_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_azf3328_codec_data *codec = runtime->private_data;
	int result = 0;
	u16 flags1;
	bool previously_muted = false;
	bool is_main_mixer_playback_codec = (AZF_CODEC_PLAYBACK == codec->type);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev_dbg(chip->card->dev, "START PCM %s\n", codec->name);

		if (is_main_mixer_playback_codec) {
			/* mute WaveOut (avoid clicking during setup) */
			previously_muted =
				snd_azf3328_mixer_mute_control_pcm(
						chip, 1
				);
		}

		snd_azf3328_codec_setfmt(codec,
			runtime->rate,
			snd_pcm_format_width(runtime->format),
			runtime->channels);

		spin_lock(codec->lock);
		/* first, remember current value: */
		flags1 = snd_azf3328_codec_inw(codec, IDX_IO_CODEC_DMA_FLAGS);

		/* stop transfer */
		flags1 &= ~DMA_RESUME;
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS, flags1);

		/* FIXME: clear interrupts or what??? */
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_IRQTYPE, 0xffff);
		spin_unlock(codec->lock);

		snd_azf3328_codec_setdmaa(chip, codec, runtime->dma_addr,
			snd_pcm_lib_period_bytes(substream),
			snd_pcm_lib_buffer_bytes(substream)
		);

		spin_lock(codec->lock);
#ifdef WIN9X
		/* FIXME: enable playback/recording??? */
		flags1 |= DMA_RUN_SOMETHING1 | DMA_RUN_SOMETHING2;
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS, flags1);

		/* start transfer again */
		/* FIXME: what is this value (0x0010)??? */
		flags1 |= DMA_RESUME | DMA_EPILOGUE_SOMETHING;
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS, flags1);
#else /* NT4 */
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS,
			0x0000);
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS,
			DMA_RUN_SOMETHING1);
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS,
			DMA_RUN_SOMETHING1 |
			DMA_RUN_SOMETHING2);
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS,
			DMA_RESUME |
			SOMETHING_ALMOST_ALWAYS_SET |
			DMA_EPILOGUE_SOMETHING |
			DMA_SOMETHING_ELSE);
#endif
		spin_unlock(codec->lock);
		snd_azf3328_ctrl_codec_activity(chip, codec->type, 1);

		if (is_main_mixer_playback_codec) {
			/* now unmute WaveOut */
			if (!previously_muted)
				snd_azf3328_mixer_mute_control_pcm(
						chip, 0
				);
		}

		dev_dbg(chip->card->dev, "PCM STARTED %s\n", codec->name);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		dev_dbg(chip->card->dev, "PCM RESUME %s\n", codec->name);
		/* resume codec if we were active */
		spin_lock(codec->lock);
		if (codec->running)
			snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS,
				snd_azf3328_codec_inw(
					codec, IDX_IO_CODEC_DMA_FLAGS
				) | DMA_RESUME
			);
		spin_unlock(codec->lock);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(chip->card->dev, "PCM STOP %s\n", codec->name);

		if (is_main_mixer_playback_codec) {
			/* mute WaveOut (avoid clicking during setup) */
			previously_muted =
				snd_azf3328_mixer_mute_control_pcm(
						chip, 1
				);
		}

		spin_lock(codec->lock);
		/* first, remember current value: */
		flags1 = snd_azf3328_codec_inw(codec, IDX_IO_CODEC_DMA_FLAGS);

		/* stop transfer */
		flags1 &= ~DMA_RESUME;
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS, flags1);

		/* hmm, is this really required? we're resetting the same bit
		 * immediately thereafter... */
		flags1 |= DMA_RUN_SOMETHING1;
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS, flags1);

		flags1 &= ~DMA_RUN_SOMETHING1;
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS, flags1);
		spin_unlock(codec->lock);
		snd_azf3328_ctrl_codec_activity(chip, codec->type, 0);

		if (is_main_mixer_playback_codec) {
			/* now unmute WaveOut */
			if (!previously_muted)
				snd_azf3328_mixer_mute_control_pcm(
						chip, 0
				);
		}

		dev_dbg(chip->card->dev, "PCM STOPPED %s\n", codec->name);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		dev_dbg(chip->card->dev, "PCM SUSPEND %s\n", codec->name);
		/* make sure codec is stopped */
		snd_azf3328_codec_outw(codec, IDX_IO_CODEC_DMA_FLAGS,
			snd_azf3328_codec_inw(
				codec, IDX_IO_CODEC_DMA_FLAGS
			) & ~DMA_RESUME
		);
		break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		WARN(1, "FIXME: SNDRV_PCM_TRIGGER_PAUSE_PUSH NIY!\n");
                break;
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		WARN(1, "FIXME: SNDRV_PCM_TRIGGER_PAUSE_RELEASE NIY!\n");
                break;
        default:
		WARN(1, "FIXME: unknown trigger mode!\n");
                return -EINVAL;
	}

	return result;
}

static snd_pcm_uframes_t
snd_azf3328_pcm_pointer(struct snd_pcm_substream *substream
)
{
	const struct snd_azf3328_codec_data *codec =
		substream->runtime->private_data;
	unsigned long result;
	snd_pcm_uframes_t frmres;

	result = snd_azf3328_codec_inl(codec, IDX_IO_CODEC_DMA_CURRPOS);

	/* calculate offset */
#ifdef QUERY_HARDWARE
	result -= snd_azf3328_codec_inl(codec, IDX_IO_CODEC_DMA_START_1);
#else
	result -= codec->dma_base;
#endif
	frmres = bytes_to_frames( substream->runtime, result);
	dev_dbg(substream->pcm->card->dev, "%08li %s @ 0x%8lx, frames %8ld\n",
		jiffies, codec->name, result, frmres);
	return frmres;
}

/******************************************************************/

#ifdef SUPPORT_GAMEPORT
static inline void
snd_azf3328_gameport_irq_enable(struct snd_azf3328 *chip,
				bool enable
)
{
	snd_azf3328_io_reg_setb(
		chip->game_io+IDX_GAME_HWCONFIG,
		GAME_HWCFG_IRQ_ENABLE,
		enable
	);
}

static inline void
snd_azf3328_gameport_legacy_address_enable(struct snd_azf3328 *chip,
					   bool enable
)
{
	snd_azf3328_io_reg_setb(
		chip->game_io+IDX_GAME_HWCONFIG,
		GAME_HWCFG_LEGACY_ADDRESS_ENABLE,
		enable
	);
}

static void
snd_azf3328_gameport_set_counter_frequency(struct snd_azf3328 *chip,
					   unsigned int freq_cfg
)
{
	snd_azf3328_io_reg_setb(
		chip->game_io+IDX_GAME_HWCONFIG,
		0x02,
		(freq_cfg & 1) != 0
	);
	snd_azf3328_io_reg_setb(
		chip->game_io+IDX_GAME_HWCONFIG,
		0x04,
		(freq_cfg & 2) != 0
	);
}

static inline void
snd_azf3328_gameport_axis_circuit_enable(struct snd_azf3328 *chip, bool enable)
{
	snd_azf3328_ctrl_reg_6AH_update(
		chip, IO_6A_SOMETHING2_GAMEPORT, enable
	);
}

static inline void
snd_azf3328_gameport_interrupt(struct snd_azf3328 *chip)
{
	/*
	 * skeleton handler only
	 * (we do not want axis reading in interrupt handler - too much load!)
	 */
	dev_dbg(chip->card->dev, "gameport irq\n");

	 /* this should ACK the gameport IRQ properly, hopefully. */
	snd_azf3328_game_inw(chip, IDX_GAME_AXIS_VALUE);
}

static int
snd_azf3328_gameport_open(struct gameport *gameport, int mode)
{
	struct snd_azf3328 *chip = gameport_get_port_data(gameport);
	int res;

	dev_dbg(chip->card->dev, "gameport_open, mode %d\n", mode);
	switch (mode) {
	case GAMEPORT_MODE_COOKED:
	case GAMEPORT_MODE_RAW:
		res = 0;
		break;
	default:
		res = -1;
		break;
	}

	snd_azf3328_gameport_set_counter_frequency(chip,
				GAME_HWCFG_ADC_COUNTER_FREQ_STD);
	snd_azf3328_gameport_axis_circuit_enable(chip, (res == 0));

	return res;
}

static void
snd_azf3328_gameport_close(struct gameport *gameport)
{
	struct snd_azf3328 *chip = gameport_get_port_data(gameport);

	dev_dbg(chip->card->dev, "gameport_close\n");
	snd_azf3328_gameport_set_counter_frequency(chip,
				GAME_HWCFG_ADC_COUNTER_FREQ_1_200);
	snd_azf3328_gameport_axis_circuit_enable(chip, 0);
}

static int
snd_azf3328_gameport_cooked_read(struct gameport *gameport,
				 int *axes,
				 int *buttons
)
{
	struct snd_azf3328 *chip = gameport_get_port_data(gameport);
	int i;
	u8 val;
	unsigned long flags;

	if (snd_BUG_ON(!chip))
		return 0;

	spin_lock_irqsave(&chip->reg_lock, flags);
	val = snd_azf3328_game_inb(chip, IDX_GAME_LEGACY_COMPATIBLE);
	*buttons = (~(val) >> 4) & 0xf;

	/* ok, this one is a bit dirty: cooked_read is being polled by a timer,
	 * thus we're atomic and cannot actively wait in here
	 * (which would be useful for us since it probably would be better
	 * to trigger a measurement in here, then wait a short amount of
	 * time until it's finished, then read values of _this_ measurement).
	 *
	 * Thus we simply resort to reading values if they're available already
	 * and trigger the next measurement.
	 */

	val = snd_azf3328_game_inb(chip, IDX_GAME_AXES_CONFIG);
	if (val & GAME_AXES_SAMPLING_READY) {
		for (i = 0; i < ARRAY_SIZE(chip->axes); ++i) {
			/* configure the axis to read */
			val = (i << 4) | 0x0f;
			snd_azf3328_game_outb(chip, IDX_GAME_AXES_CONFIG, val);

			chip->axes[i] = snd_azf3328_game_inw(
						chip, IDX_GAME_AXIS_VALUE
					);
		}
	}

	/* trigger next sampling of axes, to be evaluated the next time we
	 * enter this function */

	/* for some very, very strange reason we cannot enable
	 * Measurement Ready monitoring for all axes here,
	 * at least not when only one joystick connected */
	val = 0x03; /* we're able to monitor axes 1 and 2 only */
	snd_azf3328_game_outb(chip, IDX_GAME_AXES_CONFIG, val);

	snd_azf3328_game_outw(chip, IDX_GAME_AXIS_VALUE, 0xffff);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	for (i = 0; i < ARRAY_SIZE(chip->axes); i++) {
		axes[i] = chip->axes[i];
		if (axes[i] == 0xffff)
			axes[i] = -1;
	}

	dev_dbg(chip->card->dev, "cooked_read: axes %d %d %d %d buttons %d\n",
		axes[0], axes[1], axes[2], axes[3], *buttons);

	return 0;
}

static int
snd_azf3328_gameport(struct snd_azf3328 *chip, int dev)
{
	struct gameport *gp;

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		dev_err(chip->card->dev, "cannot alloc memory for gameport\n");
		return -ENOMEM;
	}

	gameport_set_name(gp, "AZF3328 Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gp->io = chip->game_io;
	gameport_set_port_data(gp, chip);

	gp->open = snd_azf3328_gameport_open;
	gp->close = snd_azf3328_gameport_close;
	gp->fuzz = 16; /* seems ok */
	gp->cooked_read = snd_azf3328_gameport_cooked_read;

	/* DISABLE legacy address: we don't need it! */
	snd_azf3328_gameport_legacy_address_enable(chip, 0);

	snd_azf3328_gameport_set_counter_frequency(chip,
				GAME_HWCFG_ADC_COUNTER_FREQ_1_200);
	snd_azf3328_gameport_axis_circuit_enable(chip, 0);

	gameport_register_port(chip->gameport);

	return 0;
}

static void
snd_azf3328_gameport_free(struct snd_azf3328 *chip)
{
	if (chip->gameport) {
		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
	}
	snd_azf3328_gameport_irq_enable(chip, 0);
}
#else
static inline int
snd_azf3328_gameport(struct snd_azf3328 *chip, int dev) { return -ENOSYS; }
static inline void
snd_azf3328_gameport_free(struct snd_azf3328 *chip) { }
static inline void
snd_azf3328_gameport_interrupt(struct snd_azf3328 *chip)
{
	dev_warn(chip->card->dev, "huh, game port IRQ occurred!?\n");
}
#endif /* SUPPORT_GAMEPORT */

/******************************************************************/

static inline void
snd_azf3328_irq_log_unknown_type(struct snd_azf3328 *chip, u8 which)
{
	dev_dbg(chip->card->dev,
		"unknown IRQ type (%x) occurred, please report!\n",
		which);
}

static inline void
snd_azf3328_pcm_interrupt(struct snd_azf3328 *chip,
			  const struct snd_azf3328_codec_data *first_codec,
			  u8 status
)
{
	u8 which;
	enum snd_azf3328_codec_type codec_type;
	const struct snd_azf3328_codec_data *codec = first_codec;

	for (codec_type = AZF_CODEC_PLAYBACK;
		 codec_type <= AZF_CODEC_I2S_OUT;
			 ++codec_type, ++codec) {

		/* skip codec if there's no interrupt for it */
		if (!(status & (1 << codec_type)))
			continue;

		spin_lock(codec->lock);
		which = snd_azf3328_codec_inb(codec, IDX_IO_CODEC_IRQTYPE);
		/* ack all IRQ types immediately */
		snd_azf3328_codec_outb(codec, IDX_IO_CODEC_IRQTYPE, which);
		spin_unlock(codec->lock);

		if (codec->substream) {
			snd_pcm_period_elapsed(codec->substream);
			dev_dbg(chip->card->dev, "%s period done (#%x), @ %x\n",
				codec->name,
				which,
				snd_azf3328_codec_inl(
					codec, IDX_IO_CODEC_DMA_CURRPOS));
		} else
			dev_warn(chip->card->dev, "irq handler problem!\n");
		if (which & IRQ_SOMETHING)
			snd_azf3328_irq_log_unknown_type(chip, which);
	}
}

static irqreturn_t
snd_azf3328_interrupt(int irq, void *dev_id)
{
	struct snd_azf3328 *chip = dev_id;
	u8 status;
	static unsigned long irq_count;

	status = snd_azf3328_ctrl_inb(chip, IDX_IO_IRQSTATUS);

        /* fast path out, to ease interrupt sharing */
	if (!(status &
		(IRQ_PLAYBACK|IRQ_RECORDING|IRQ_I2S_OUT
		|IRQ_GAMEPORT|IRQ_MPU401|IRQ_TIMER)
	))
		return IRQ_NONE; /* must be interrupt for another device */

	dev_dbg(chip->card->dev,
		"irq_count %ld! IDX_IO_IRQSTATUS %04x\n",
			irq_count++ /* debug-only */,
			status);

	if (status & IRQ_TIMER) {
		/* dev_dbg(chip->card->dev, "timer %ld\n",
			snd_azf3328_codec_inl(chip, IDX_IO_TIMER_VALUE)
				& TIMER_VALUE_MASK
		); */
		if (chip->timer)
			snd_timer_interrupt(chip->timer, chip->timer->sticks);
		/* ACK timer */
                spin_lock(&chip->reg_lock);
		snd_azf3328_ctrl_outb(chip, IDX_IO_TIMER_VALUE + 3, 0x07);
		spin_unlock(&chip->reg_lock);
		dev_dbg(chip->card->dev, "timer IRQ\n");
	}

	if (status & (IRQ_PLAYBACK|IRQ_RECORDING|IRQ_I2S_OUT))
		snd_azf3328_pcm_interrupt(chip, chip->codecs, status);

	if (status & IRQ_GAMEPORT)
		snd_azf3328_gameport_interrupt(chip);

	/* MPU401 has less critical IRQ requirements
	 * than timer and playback/recording, right? */
	if (status & IRQ_MPU401) {
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data);

		/* hmm, do we have to ack the IRQ here somehow?
		 * If so, then I don't know how yet... */
		dev_dbg(chip->card->dev, "MPU401 IRQ\n");
	}
	return IRQ_HANDLED;
}

/*****************************************************************/

/* as long as we think we have identical snd_pcm_hardware parameters
   for playback, capture and i2s out, we can use the same physical struct
   since the struct is simply being copied into a member.
*/
static const struct snd_pcm_hardware snd_azf3328_hardware =
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
	.rate_min =		AZF_FREQ_4000,
	.rate_max =		AZF_FREQ_66200,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(64*1024),
	.period_bytes_min =	1024,
	.period_bytes_max =	(32*1024),
	/* We simply have two DMA areas (instead of a list of descriptors
	   such as other cards); I believe that this is a fixed hardware
	   attribute and there isn't much driver magic to be done to expand it.
	   Thus indicate that we have at least and at most 2 periods. */
	.periods_min =		2,
	.periods_max =		2,
	/* FIXME: maybe that card actually has a FIFO?
	 * Hmm, it seems newer revisions do have one, but we still don't know
	 * its size... */
	.fifo_size =		0,
};


static const unsigned int snd_azf3328_fixed_rates[] = {
	AZF_FREQ_4000,
	AZF_FREQ_4800,
	AZF_FREQ_5512,
	AZF_FREQ_6620,
	AZF_FREQ_8000,
	AZF_FREQ_9600,
	AZF_FREQ_11025,
	AZF_FREQ_13240,
	AZF_FREQ_16000,
	AZF_FREQ_22050,
	AZF_FREQ_32000,
	AZF_FREQ_44100,
	AZF_FREQ_48000,
	AZF_FREQ_66200
};

static const struct snd_pcm_hw_constraint_list snd_azf3328_hw_constraints_rates = {
	.count = ARRAY_SIZE(snd_azf3328_fixed_rates),
	.list = snd_azf3328_fixed_rates,
	.mask = 0,
};

/*****************************************************************/

static int
snd_azf3328_pcm_open(struct snd_pcm_substream *substream,
		     enum snd_azf3328_codec_type codec_type
)
{
	struct snd_azf3328 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_azf3328_codec_data *codec = &chip->codecs[codec_type];

	codec->substream = substream;

	/* same parameters for all our codecs - at least we think so... */
	runtime->hw = snd_azf3328_hardware;

	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &snd_azf3328_hw_constraints_rates);
	runtime->private_data = codec;
	return 0;
}

static int
snd_azf3328_pcm_playback_open(struct snd_pcm_substream *substream)
{
	return snd_azf3328_pcm_open(substream, AZF_CODEC_PLAYBACK);
}

static int
snd_azf3328_pcm_capture_open(struct snd_pcm_substream *substream)
{
	return snd_azf3328_pcm_open(substream, AZF_CODEC_CAPTURE);
}

static int
snd_azf3328_pcm_i2s_out_open(struct snd_pcm_substream *substream)
{
	return snd_azf3328_pcm_open(substream, AZF_CODEC_I2S_OUT);
}

static int
snd_azf3328_pcm_close(struct snd_pcm_substream *substream
)
{
	struct snd_azf3328_codec_data *codec =
		substream->runtime->private_data;

	codec->substream = NULL;
	return 0;
}

/******************************************************************/

static const struct snd_pcm_ops snd_azf3328_playback_ops = {
	.open =		snd_azf3328_pcm_playback_open,
	.close =	snd_azf3328_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_azf3328_hw_params,
	.hw_free =	snd_azf3328_hw_free,
	.prepare =	snd_azf3328_pcm_prepare,
	.trigger =	snd_azf3328_pcm_trigger,
	.pointer =	snd_azf3328_pcm_pointer
};

static const struct snd_pcm_ops snd_azf3328_capture_ops = {
	.open =		snd_azf3328_pcm_capture_open,
	.close =	snd_azf3328_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_azf3328_hw_params,
	.hw_free =	snd_azf3328_hw_free,
	.prepare =	snd_azf3328_pcm_prepare,
	.trigger =	snd_azf3328_pcm_trigger,
	.pointer =	snd_azf3328_pcm_pointer
};

static const struct snd_pcm_ops snd_azf3328_i2s_out_ops = {
	.open =		snd_azf3328_pcm_i2s_out_open,
	.close =	snd_azf3328_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_azf3328_hw_params,
	.hw_free =	snd_azf3328_hw_free,
	.prepare =	snd_azf3328_pcm_prepare,
	.trigger =	snd_azf3328_pcm_trigger,
	.pointer =	snd_azf3328_pcm_pointer
};

static int
snd_azf3328_pcm(struct snd_azf3328 *chip)
{
	/* pcm devices */
	enum { AZF_PCMDEV_STD, AZF_PCMDEV_I2S_OUT, NUM_AZF_PCMDEVS };

	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(chip->card, "AZF3328 DSP", AZF_PCMDEV_STD,
								1, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
						&snd_azf3328_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
						&snd_azf3328_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->shortname);
	/* same pcm object for playback/capture (see snd_pcm_new() above) */
	chip->pcm[AZF_CODEC_PLAYBACK] = pcm;
	chip->pcm[AZF_CODEC_CAPTURE] = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						snd_dma_pci_data(chip->pci),
							64*1024, 64*1024);

	err = snd_pcm_new(chip->card, "AZF3328 I2S OUT", AZF_PCMDEV_I2S_OUT,
								1, 0, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
						&snd_azf3328_i2s_out_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcm[AZF_CODEC_I2S_OUT] = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						snd_dma_pci_data(chip->pci),
							64*1024, 64*1024);

	return 0;
}

/******************************************************************/

/*** NOTE: the physical timer resolution actually is 1024000 ticks per second
 *** (probably derived from main crystal via a divider of 24),
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

	chip = snd_timer_chip(timer);
	delay = ((timer->sticks * seqtimer_scaling) - 1) & TIMER_VALUE_MASK;
	if (delay < 49) {
		/* uhoh, that's not good, since user-space won't know about
		 * this timing tweak
		 * (we need to do it to avoid a lockup, though) */

		dev_dbg(chip->card->dev, "delay was too low (%d)!\n", delay);
		delay = 49; /* minimum time is 49 ticks */
	}
	dev_dbg(chip->card->dev, "setting timer countdown value %d\n", delay);
	delay |= TIMER_COUNTDOWN_ENABLE | TIMER_IRQ_ENABLE;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_azf3328_ctrl_outl(chip, IDX_IO_TIMER_VALUE, delay);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int
snd_azf3328_timer_stop(struct snd_timer *timer)
{
	struct snd_azf3328 *chip;
	unsigned long flags;

	chip = snd_timer_chip(timer);
	spin_lock_irqsave(&chip->reg_lock, flags);
	/* disable timer countdown and interrupt */
	/* Hmm, should we write TIMER_IRQ_ACK here?
	   YES indeed, otherwise a rogue timer operation - which prompts
	   ALSA(?) to call repeated stop() in vain, but NOT start() -
	   will never end (value 0x03 is kept shown in control byte).
	   Simply manually poking 0x04 _once_ immediately successfully stops
	   the hardware/ALSA interrupt activity. */
	snd_azf3328_ctrl_outb(chip, IDX_IO_TIMER_VALUE + 3, 0x04);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}


static int
snd_azf3328_timer_precise_resolution(struct snd_timer *timer,
					       unsigned long *num, unsigned long *den)
{
	*num = 1;
	*den = 1024000 / seqtimer_scaling;
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

static int
snd_azf3328_timer(struct snd_azf3328 *chip, int device)
{
	struct snd_timer *timer = NULL;
	struct snd_timer_id tid;
	int err;

	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = chip->card->number;
	tid.device = device;
	tid.subdevice = 0;

	snd_azf3328_timer_hw.resolution *= seqtimer_scaling;
	snd_azf3328_timer_hw.ticks /= seqtimer_scaling;

	err = snd_timer_new(chip->card, "AZF3328", &tid, &timer);
	if (err < 0)
		goto out;

	strcpy(timer->name, "AZF3328 timer");
	timer->private_data = chip;
	timer->hw = snd_azf3328_timer_hw;

	chip->timer = timer;

	snd_azf3328_timer_stop(timer);

	err = 0;

out:
	return err;
}

/******************************************************************/

static int
snd_azf3328_free(struct snd_azf3328 *chip)
{
	if (chip->irq < 0)
		goto __end_hw;

	snd_azf3328_mixer_reset(chip);

	snd_azf3328_timer_stop(chip->timer);
	snd_azf3328_gameport_free(chip);

__end_hw:
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
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

#if 0
/* check whether a bit can be modified */
static void
snd_azf3328_test_bit(unsigned unsigned reg, int bit)
{
	unsigned char val, valoff, valon;

	val = inb(reg);

	outb(val & ~(1 << bit), reg);
	valoff = inb(reg);

	outb(val|(1 << bit), reg);
	valon = inb(reg);

	outb(val, reg);

	printk(KERN_DEBUG "reg %04x bit %d: %02x %02x %02x\n",
				reg, bit, val, valoff, valon
	);
}
#endif

static inline void
snd_azf3328_debug_show_ports(const struct snd_azf3328 *chip)
{
	u16 tmp;

	dev_dbg(chip->card->dev,
		"ctrl_io 0x%lx, game_io 0x%lx, mpu_io 0x%lx, "
		"opl3_io 0x%lx, mixer_io 0x%lx, irq %d\n",
		chip->ctrl_io, chip->game_io, chip->mpu_io,
		chip->opl3_io, chip->mixer_io, chip->irq);

	dev_dbg(chip->card->dev,
		"game %02x %02x %02x %02x %02x %02x\n",
		snd_azf3328_game_inb(chip, 0),
		snd_azf3328_game_inb(chip, 1),
		snd_azf3328_game_inb(chip, 2),
		snd_azf3328_game_inb(chip, 3),
		snd_azf3328_game_inb(chip, 4),
		snd_azf3328_game_inb(chip, 5));

	for (tmp = 0; tmp < 0x07; tmp += 1)
		dev_dbg(chip->card->dev,
			"mpu_io 0x%04x\n", inb(chip->mpu_io + tmp));

	for (tmp = 0; tmp <= 0x07; tmp += 1)
		dev_dbg(chip->card->dev,
			"0x%02x: game200 0x%04x, game208 0x%04x\n",
			tmp, inb(0x200 + tmp), inb(0x208 + tmp));

	for (tmp = 0; tmp <= 0x01; tmp += 1)
		dev_dbg(chip->card->dev,
			"0x%02x: mpu300 0x%04x, mpu310 0x%04x, mpu320 0x%04x, "
			"mpu330 0x%04x opl388 0x%04x opl38c 0x%04x\n",
				tmp,
				inb(0x300 + tmp),
				inb(0x310 + tmp),
				inb(0x320 + tmp),
				inb(0x330 + tmp),
				inb(0x388 + tmp),
				inb(0x38c + tmp));

	for (tmp = 0; tmp < AZF_IO_SIZE_CTRL; tmp += 2)
		dev_dbg(chip->card->dev,
			"ctrl 0x%02x: 0x%04x\n",
			tmp, snd_azf3328_ctrl_inw(chip, tmp));

	for (tmp = 0; tmp < AZF_IO_SIZE_MIXER; tmp += 2)
		dev_dbg(chip->card->dev,
			"mixer 0x%02x: 0x%04x\n",
			tmp, snd_azf3328_mixer_inw(chip, tmp));
}

static int
snd_azf3328_create(struct snd_card *card,
		   struct pci_dev *pci,
		   unsigned long device_type,
		   struct snd_azf3328 **rchip)
{
	struct snd_azf3328 *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =     snd_azf3328_dev_free,
	};
	u8 dma_init;
	enum snd_azf3328_codec_type codec_type;
	struct snd_azf3328_codec_data *codec_setup;

	*rchip = NULL;

	err = pci_enable_device(pci);
	if (err < 0)
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
	if (dma_set_mask(&pci->dev, DMA_BIT_MASK(24)) < 0 ||
	    dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(24)) < 0) {
		dev_err(card->dev,
			"architecture does not support 24bit PCI busmaster DMA\n"
		);
		err = -ENXIO;
		goto out_err;
	}

	err = pci_request_regions(pci, "Aztech AZF3328");
	if (err < 0)
		goto out_err;

	chip->ctrl_io  = pci_resource_start(pci, 0);
	chip->game_io  = pci_resource_start(pci, 1);
	chip->mpu_io   = pci_resource_start(pci, 2);
	chip->opl3_io  = pci_resource_start(pci, 3);
	chip->mixer_io = pci_resource_start(pci, 4);

	codec_setup = &chip->codecs[AZF_CODEC_PLAYBACK];
	codec_setup->io_base = chip->ctrl_io + AZF_IO_OFFS_CODEC_PLAYBACK;
	codec_setup->lock = &chip->reg_lock;
	codec_setup->type = AZF_CODEC_PLAYBACK;
	codec_setup->name = "PLAYBACK";

	codec_setup = &chip->codecs[AZF_CODEC_CAPTURE];
	codec_setup->io_base = chip->ctrl_io + AZF_IO_OFFS_CODEC_CAPTURE;
	codec_setup->lock = &chip->reg_lock;
	codec_setup->type = AZF_CODEC_CAPTURE;
	codec_setup->name = "CAPTURE";

	codec_setup = &chip->codecs[AZF_CODEC_I2S_OUT];
	codec_setup->io_base = chip->ctrl_io + AZF_IO_OFFS_CODEC_I2S_OUT;
	codec_setup->lock = &chip->reg_lock;
	codec_setup->type = AZF_CODEC_I2S_OUT;
	codec_setup->name = "I2S_OUT";

	if (request_irq(pci->irq, snd_azf3328_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, chip)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		err = -EBUSY;
		goto out_err;
	}
	chip->irq = pci->irq;
	pci_set_master(pci);
	synchronize_irq(chip->irq);

	snd_azf3328_debug_show_ports(chip);

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0)
		goto out_err;

	/* create mixer interface & switches */
	err = snd_azf3328_mixer_new(chip);
	if (err < 0)
		goto out_err;

	/* standard codec init stuff */
		/* default DMA init value */
	dma_init = DMA_RUN_SOMETHING2|DMA_EPILOGUE_SOMETHING|DMA_SOMETHING_ELSE;

	for (codec_type = AZF_CODEC_PLAYBACK;
		codec_type <= AZF_CODEC_I2S_OUT; ++codec_type) {
		struct snd_azf3328_codec_data *codec =
			 &chip->codecs[codec_type];

		/* shutdown codecs to reduce power / noise */
			/* have ...ctrl_codec_activity() act properly */
		codec->running = 1;
		snd_azf3328_ctrl_codec_activity(chip, codec_type, 0);

		spin_lock_irq(codec->lock);
		snd_azf3328_codec_outb(codec, IDX_IO_CODEC_DMA_FLAGS,
						 dma_init);
		spin_unlock_irq(codec->lock);
	}

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

static int
snd_azf3328_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_azf3328 *chip;
	struct snd_opl3 *opl3;
	int err;

	if (dev >= SNDRV_CARDS) {
		err = -ENODEV;
		goto out;
	}
	if (!enable[dev]) {
		dev++;
		err = -ENOENT;
		goto out;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0)
		goto out;

	strcpy(card->driver, "AZF3328");
	strcpy(card->shortname, "Aztech AZF3328 (PCI168)");

	err = snd_azf3328_create(card, pci, pci_id->driver_data, &chip);
	if (err < 0)
		goto out_err;

	card->private_data = chip;

	/* chose to use MPU401_HW_AZT2320 ID instead of MPU401_HW_MPU401,
	   since our hardware ought to be similar, thus use same ID. */
	err = snd_mpu401_uart_new(
		card, 0,
		MPU401_HW_AZT2320, chip->mpu_io,
		MPU401_INFO_INTEGRATED | MPU401_INFO_IRQ_HOOK,
		-1, &chip->rmidi
	);
	if (err < 0) {
		dev_err(card->dev, "no MPU-401 device at 0x%lx?\n",
				chip->mpu_io
		);
		goto out_err;
	}

	err = snd_azf3328_timer(chip, 0);
	if (err < 0)
		goto out_err;

	err = snd_azf3328_pcm(chip);
	if (err < 0)
		goto out_err;

	if (snd_opl3_create(card, chip->opl3_io, chip->opl3_io+2,
			    OPL3_HW_AUTO, 1, &opl3) < 0) {
		dev_err(card->dev, "no OPL3 device at 0x%lx-0x%lx?\n",
			   chip->opl3_io, chip->opl3_io+2
		);
	} else {
		/* need to use IDs 1, 2 since ID 0 is snd_azf3328_timer above */
		err = snd_opl3_timer_new(opl3, 1, 2);
		if (err < 0)
			goto out_err;
		err = snd_opl3_hwdep_new(opl3, 0, 1, NULL);
		if (err < 0)
			goto out_err;
		opl3->private_data = chip;
	}

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->ctrl_io, chip->irq);

	err = snd_card_register(card);
	if (err < 0)
		goto out_err;

#ifdef MODULE
	dev_info(card->dev,
		 "Sound driver for Aztech AZF3328-based soundcards such as PCI168.\n");
	dev_info(card->dev,
		 "Hardware was completely undocumented, unfortunately.\n");
	dev_info(card->dev,
		 "Feel free to contact andi AT lisas.de for bug reports etc.!\n");
	dev_info(card->dev,
		 "User-scalable sequencer timer set to %dHz (1024000Hz / %d).\n",
		 1024000 / seqtimer_scaling, seqtimer_scaling);
#endif

	snd_azf3328_gameport(chip, dev);

	pci_set_drvdata(pci, card);
	dev++;

	err = 0;
	goto out;

out_err:
	dev_err(card->dev, "something failed, exiting\n");
	snd_card_free(card);

out:
	return err;
}

static void
snd_azf3328_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

#ifdef CONFIG_PM_SLEEP
static inline void
snd_azf3328_suspend_regs(const struct snd_azf3328 *chip,
			 unsigned long io_addr, unsigned count, u32 *saved_regs)
{
	unsigned reg;

	for (reg = 0; reg < count; ++reg) {
		*saved_regs = inl(io_addr);
		dev_dbg(chip->card->dev, "suspend: io 0x%04lx: 0x%08x\n",
			io_addr, *saved_regs);
		++saved_regs;
		io_addr += sizeof(*saved_regs);
	}
}

static inline void
snd_azf3328_resume_regs(const struct snd_azf3328 *chip,
			const u32 *saved_regs,
			unsigned long io_addr,
			unsigned count
)
{
	unsigned reg;

	for (reg = 0; reg < count; ++reg) {
		outl(*saved_regs, io_addr);
		dev_dbg(chip->card->dev,
			"resume: io 0x%04lx: 0x%08x --> 0x%08x\n",
			io_addr, *saved_regs, inl(io_addr));
		++saved_regs;
		io_addr += sizeof(*saved_regs);
	}
}

static inline void
snd_azf3328_suspend_ac97(struct snd_azf3328 *chip)
{
#ifdef AZF_USE_AC97_LAYER
	snd_ac97_suspend(chip->ac97);
#else
	snd_azf3328_suspend_regs(chip, chip->mixer_io,
		ARRAY_SIZE(chip->saved_regs_mixer), chip->saved_regs_mixer);

	/* make sure to disable master volume etc. to prevent looping sound */
	snd_azf3328_mixer_mute_control_master(chip, 1);
	snd_azf3328_mixer_mute_control_pcm(chip, 1);
#endif /* AZF_USE_AC97_LAYER */
}

static inline void
snd_azf3328_resume_ac97(const struct snd_azf3328 *chip)
{
#ifdef AZF_USE_AC97_LAYER
	snd_ac97_resume(chip->ac97);
#else
	snd_azf3328_resume_regs(chip, chip->saved_regs_mixer, chip->mixer_io,
					ARRAY_SIZE(chip->saved_regs_mixer));

	/* unfortunately with 32bit transfers, IDX_MIXER_PLAY_MASTER (0x02)
	   and IDX_MIXER_RESET (offset 0x00) get touched at the same time,
	   resulting in a mixer reset condition persisting until _after_
	   master vol was restored. Thus master vol needs an extra restore. */
	outw(((u16 *)chip->saved_regs_mixer)[1], chip->mixer_io + 2);
#endif /* AZF_USE_AC97_LAYER */
}

static int
snd_azf3328_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_azf3328 *chip = card->private_data;
	u16 *saved_regs_ctrl_u16;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	snd_azf3328_suspend_ac97(chip);

	snd_azf3328_suspend_regs(chip, chip->ctrl_io,
		ARRAY_SIZE(chip->saved_regs_ctrl), chip->saved_regs_ctrl);

	/* manually store the one currently relevant write-only reg, too */
	saved_regs_ctrl_u16 = (u16 *)chip->saved_regs_ctrl;
	saved_regs_ctrl_u16[IDX_IO_6AH / 2] = chip->shadow_reg_ctrl_6AH;

	snd_azf3328_suspend_regs(chip, chip->game_io,
		ARRAY_SIZE(chip->saved_regs_game), chip->saved_regs_game);
	snd_azf3328_suspend_regs(chip, chip->mpu_io,
		ARRAY_SIZE(chip->saved_regs_mpu), chip->saved_regs_mpu);
	snd_azf3328_suspend_regs(chip, chip->opl3_io,
		ARRAY_SIZE(chip->saved_regs_opl3), chip->saved_regs_opl3);
	return 0;
}

static int
snd_azf3328_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	const struct snd_azf3328 *chip = card->private_data;

	snd_azf3328_resume_regs(chip, chip->saved_regs_game, chip->game_io,
					ARRAY_SIZE(chip->saved_regs_game));
	snd_azf3328_resume_regs(chip, chip->saved_regs_mpu, chip->mpu_io,
					ARRAY_SIZE(chip->saved_regs_mpu));
	snd_azf3328_resume_regs(chip, chip->saved_regs_opl3, chip->opl3_io,
					ARRAY_SIZE(chip->saved_regs_opl3));

	snd_azf3328_resume_ac97(chip);

	snd_azf3328_resume_regs(chip, chip->saved_regs_ctrl, chip->ctrl_io,
					ARRAY_SIZE(chip->saved_regs_ctrl));

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_azf3328_pm, snd_azf3328_suspend, snd_azf3328_resume);
#define SND_AZF3328_PM_OPS	&snd_azf3328_pm
#else
#define SND_AZF3328_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct pci_driver azf3328_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_azf3328_ids,
	.probe = snd_azf3328_probe,
	.remove = snd_azf3328_remove,
	.driver = {
		.pm = SND_AZF3328_PM_OPS,
	},
};

module_pci_driver(azf3328_driver);
